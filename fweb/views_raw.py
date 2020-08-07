from django.core.servers.basehttp import FileWrapper
from django.template.response import TemplateResponse
from django.http import HttpResponse

import settings

import fitsimage
import os, shutil
import re
import pyfits
import numpy as np
import datetime
import posixpath

from utils import fix_remote_path
from utils import permission_required_or_403, assert_permission
from views_images import find_image

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.patches import Ellipse

from fweb.models import Images

import coadd
import postprocess

def get_time_from_filename(filename):
    m = re.match("^(\d\d\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(\d\d\d)", filename)
    if m:
        t = [int(m.group(i)) for i in range(1,8)]
        return datetime.datetime(t[0], t[1], t[2], t[3], t[4], t[5], t[6]*1000)

def get_index(dir):
    index = []

    if os.path.exists(dir + '/0index.txt'):
        with open(dir + '/0index.txt', 'r') as file:
            l = file.readlines()

            for line in l:
                path, filter, cover, lamp, ii, focus, pos0, pos1, wcs, ra, dec = line.split()
                index.append({'path':path, 'filter':filter, 'cover':cover, 'lamp':lamp, 'ii':ii,
                              'focus':focus, 'pos0':pos0, 'pos1':pos1, 'wcs':wcs, 'ra':ra, 'dec':dec})

    return index

@permission_required_or_403('fweb.access_raw')
def raw_list(request, channel=0):
    context = {}

    if settings.CHANNEL_ID > 0 or channel > 0:
        base = fix_remote_path(settings.BASE_RAW, channel_id = channel)
        dirs = [d for d in os.listdir(base) if os.path.isdir(posixpath.join(base, d)) and re.match('^\d{8}_\d{6}$', d)]
        raws = []
        prev_time = None

        for path in sorted(dirs, reverse=True):
            index = get_index(posixpath.join(base, path))
            length = len(index)
            is_new = False

            if index:
                first = index[0]
                last = index[-1]

                # Is the time delay between last file and current one large enough?
                if prev_time and prev_time - get_time_from_filename(last['path']) > datetime.timedelta(seconds=1):
                    is_new = True

                prev_time = get_time_from_filename(first['path'])
            else:
                first = None

            raws.append({'path':path, 'first':first, 'length':length, 'is_new':is_new})

        stat = os.statvfs(base)
        s_free = stat.f_bavail*stat.f_frsize
        s_total = stat.f_blocks*stat.f_frsize

        context['raw_list'] = raws
        context['base'] = base
        context['free'] = s_free
        context['total'] = s_total

    context['channel_list'] = range(1, settings.NCHANNELS + 1)

    context['channel'] = int(channel) if channel else 0

    return TemplateResponse(request, 'raw_list.html', context=context)

@permission_required_or_403('fweb.access_raw')
def raw_dir(request, dir='', channel=0):
    base = fix_remote_path(settings.BASE_RAW, channel_id = channel)
    fits = get_index(posixpath.join(base,  dir))
    has_index = True

    if not fits:
        fits = [f for f in os.listdir(posixpath.join(base, dir)) if os.path.isfile(posixpath.join(base, dir, f)) and re.match('^.*\.fits$', f)]
        fits = [{'path':path, 'filter':None } for path in sorted(fits)]
        has_index = False

    stat = os.statvfs(base)
    s_free = stat.f_bavail*stat.f_frsize
    s_total = stat.f_blocks*stat.f_frsize

    context={'raw_dir':fits,
             'dir':dir,
             'base':posixpath.join(base, dir),
             'free':s_free,
             'total':s_total,
             'has_index':has_index,
             'channel':channel if channel else 0}

    return TemplateResponse(request, 'raw_list.html', context = context)

@permission_required_or_403('fweb.access_raw')
def raw_view(request, filename='', channel=0, size=0, processed=False, type = 'jpeg'):
    base = fix_remote_path(settings.BASE_RAW, channel_id = channel)
    fullname = posixpath.join(base, filename)
    image = pyfits.getdata(fullname, -1)
    header = pyfits.getheader(fullname, -1)
    time = postprocess.get_time_from_filename(posixpath.split(filename)[-1])

    if processed:
        darkname = find_image(time, 'dark', channel)
        darkname = fix_remote_path(darkname, channel)

        if posixpath.exists(darkname):
            dark = pyfits.getdata(darkname, -1)
            image -= dark

    if type == 'jpeg':
        img = fitsimage.FitsImageFromData(image, image.shape[1], image.shape[0], contrast="percentile", contrast_opts={'max_percent':99.9}, scale="linear")
        if size:
            img.thumbnail((size,size))#, resample=fitsimage.Image.ANTIALIAS)
        # now what?
        response = HttpResponse(content_type="image/jpeg")
        img.save(response, "JPEG", quality=95)
    elif type == 'fits':
        response = HttpResponse(FileWrapper(file(posixpath.join(base, filename))), content_type='application/octet-stream')
        response['Content-Disposition'] = 'attachment; filename='+os.path.split(filename)[-1]
        response['Content-Length'] = os.path.getsize(posixpath.join(base, filename))

    return response

@permission_required_or_403('fweb.access_raw')
def raw_info(request, filename='', channel=0):
    base = fix_remote_path(settings.BASE_RAW, channel_id = channel)
    fits = pyfits.open(posixpath.join(base, filename))
    time = postprocess.get_time_from_filename(posixpath.split(filename)[-1])
    hdr = fits[0].header
    ignored_keywords = ['COMMENT', 'SIMPLE', 'BZERO', 'BSCALE', 'EXTEND']
    keywords = [{'key':k, 'value':repr(hdr[k]), 'comment':hdr.comments[k]} for k in hdr.keys() if k not in ignored_keywords]
    fits.close()

    context={'filename':filename,
             'keywords':keywords,
             'channel':channel if channel else 0,
             'time':time}

    avg = Images.objects.filter(type='avg',channel_id=channel,time__lt=time).order_by('-time').first()
    survey = Images.objects.filter(type='survey',channel_id=channel,time__lt=time).order_by('-time').first()
    dark = Images.objects.filter(type='dark',channel_id=channel,time__lt=time).order_by('-time').first()

    if dark:
        context['dark'] = dark

    if avg:
        context['avg'] = avg

    if survey and (time - survey.time).total_seconds() < 20*60:
        context['survey'] = survey

    return TemplateResponse(request, 'raw_info.html', context = context)

@permission_required_or_403('fweb.access_raw')
def raw_histogram(request, filename='', channel=0):
    base = fix_remote_path(settings.BASE_RAW, channel_id = channel)
    fits = pyfits.open(posixpath.join(base, filename))
    d = fits[0].data.flatten()

    fig = Figure(facecolor='white', figsize=(800/72, 600/72))
    ax = fig.add_subplot(111)

    ax.hist(d, bins=d.max()-d.min(), histtype='step', log=True)

    ax.set_xlabel("Value")
    ax.set_ylabel("")
    ax.set_title("Histogram: %s" % (filename))

    # ax.set_xlim(0, width)
    # ax.set_ylim(height, 0)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_raw')
def raw_slice(request, filename='', channel=0):
    base = fix_remote_path(settings.BASE_RAW, channel_id = channel)
    fits = pyfits.open(posixpath.join(base, filename))
    d = fits[0].data
    h,w = fits[0].shape
    y0 = int(h/2)
    x0 = int(w/2)

    fig = Figure(facecolor='white', figsize=(800/72, 600/72))

    ax = fig.add_subplot(211)
    ax.plot(d[y0], drawstyle='steps')
    ax.set_xlabel("X")
    ax.set_ylabel("Value")
    ax.set_title("Horizontal and Vertical Slices at the Center")
    ax.set_ylim(min(0, np.min(d[y0])), np.max(d[y0]))
    ax.set_xlim(0, w)

    ax = fig.add_subplot(212)
    ax.plot(d[:,x0], drawstyle='steps')
    ax.set_xlabel("Y")
    ax.set_ylabel("Value")
    #ax.set_title("Vertical Slice")
    ax.set_ylim(min(0, np.min(d[:,x0])), np.max(d[:,x0]))
    ax.set_xlim(0, h)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_raw')
def raw_mosaic(request, filename='', channel=0, size=1600):
    time = postprocess.get_time_from_filename(posixpath.split(filename)[-1])
    filenames = []

    pp = postprocess.postprocessor()

    for id in xrange(1,settings.NCHANNELS + 1):
        raws = pp.find_raw_files(time - datetime.timedelta(seconds=0.05), time + datetime.timedelta(seconds=0.05), channel_id=id)
        if raws:
            filename1 = raws[0]['fullname']
            filenames.append(filename1)

    print filenames
    mosaic = coadd.coadd_mosaic(filenames, clean=True)

    response = raw_view(request, filename=mosaic, size=1600, type='jpeg')

    os.unlink(mosaic)

    return response
