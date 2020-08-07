from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.core.servers.basehttp import FileWrapper
from django.shortcuts import redirect

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure

import settings
from models import Images

import fitsimage
import os, sys, posixpath
from astropy.io import fits as pyfits
from astropy import wcs as pywcs

import numpy as np
import StringIO

from resolve import resolve

from utils import fix_remote_path, db_query
from utils import permission_required_or_403, assert_permission

from calibrate import Calibrator

@permission_required_or_403('fweb.access_data')
def image_processed(request, id, size=800, base=settings.BASE, raw=False, format='jpeg', pmin=0.5, pmax=97):
    if request.method == 'GET':
        pmin = float(request.GET.get('pmin', pmin))
        pmax = float(request.GET.get('pmax', pmax))

    image_obj = Images.objects.get(id=id)
    filename = image_obj.filename

    filename = posixpath.join(base, filename)
    filename = fix_remote_path(filename, image_obj.channel_id)
    data = pyfits.getdata(filename, -1).astype(np.float64)
    header = pyfits.getheader(filename, -1)

    np.seterr(divide='ignore')

    if not raw and image_obj.type not in ['coadd', 'dark', 'mdark', 'flat', 'flat_11', 'bgflat', 'skyflat', 'skyflat_11', 'superflat']:
        darkname = find_image(image_obj.time, 'dark', image_obj.channel_id)
        darkname = fix_remote_path(darkname, image_obj.channel_id)
        is_flat = False

        if request.method == 'GET' and request.GET.has_key('linearize'):
            calib = Calibrator(shutter=int(image_obj.keywords['SHUTTER']), channel_id=image_obj.channel_id)
            data,header = calib.calibrate(data,header)
            darkname = None
            is_flat = True

        if darkname and posixpath.exists(darkname):
            dark = pyfits.getdata(darkname, -1)

            # if image_obj.type == 'avg' and np.median(data) < np.median(dark):
            #     # AVG image affected by off-by-one error (summed over 99 frames instead of 100, but divided by 100)
            #     data *= 100.0/99.0

            data -= dark
            is_flat = True

        if is_flat:
            flatname = find_image(image_obj.time, 'superflat', image_obj.channel_id)
            flatname = fix_remote_path(flatname, image_obj.channel_id)

            if flatname and posixpath.exists(flatname):
                flat = pyfits.getdata(flatname, -1)

                flat[np.isnan(flat)] = np.median(flat[~np.isnan(flat)])

                data *= np.mean(flat)/flat

    if raw and request.method == 'GET' and request.GET.has_key('linearize'):
        calib = Calibrator(shutter=int(image_obj.keywords['SHUTTER']), channel_id=image_obj.channel_id)
        data,header = calib.calibrate(data,header)

    if format == 'jpeg':
        img = fitsimage.FitsImageFromData(data, data.shape[1], data.shape[0], contrast="percentile", contrast_opts={'min_percent':pmin,'max_percent':pmax}, scale="linear")
        if size:
            img.thumbnail((size,size), resample=fitsimage.Image.ANTIALIAS)
        # now what?
        response = HttpResponse(content_type="image/jpeg")
        img.save(response, "JPEG", quality=95)
        return response
    elif format == 'fits':
        newname = posixpath.splitext(posixpath.split(filename)[-1])[0]+'.processed.fits'
        obj = StringIO.StringIO()
        pyfits.writeto(obj, data, header=header, clobber=True)

        response = HttpResponse(obj.getvalue(), content_type='application/octet-stream')
        response['Content-Disposition'] = 'attachment; filename='+newname
        return response

@permission_required_or_403('fweb.download_data')
def image_download(request, id, size=800, base=settings.BASE):
    image_obj = Images.objects.get(id=id)

    filename = Images.objects.get(id=id).filename
    filename = posixpath.join(base, filename)
    filename = fix_remote_path(filename, image_obj.channel_id)

    response = HttpResponse(FileWrapper(file(filename)), content_type='application/octet-stream')
    response['Content-Disposition'] = 'attachment; filename='+os.path.split(filename)[-1]
    response['Content-Length'] = os.path.getsize(filename)
    return response

@permission_required_or_403('fweb.access_data')
def images_list(request, night='', type='all', ra='', dec='', sr='5.0', channel=0):
    if request.method == 'POST':
        coords = request.POST.get('coords')
        sr = float(request.POST.get('sr'))
        type = request.POST.get('type')
        name, ra, dec = resolve(coords)

        if not type or type == 'all':
            return redirect('/images/coords/%g/%g/%g' %(ra, dec, sr))
        else:
            return redirect('/images/coords/%g/%g/%g/type/%s' %(ra, dec, sr, type))

    if ra and dec:
        # Positional query
        ra = float(ra)
        dec = float(dec)
        sr = float(sr) if sr else 5.0
        images = Images.objects.extra(select={'filter_string':'get_filter_name(filter)'},
                                      where=["q3c_radial_query(ra0, dec0, %s, %s, %s)"],
                                      params = (ra, dec, sr)).order_by('-time')
        #images = Images.objects.raw("SELECT *, get_filter_name(filter) AS filter_string FROM images WHERE q3c_radial_query(ra0, dec0, %s, %s, %s) ORDER BY time DESC", (ra, dec, sr))
        query_type = 'positional'
    elif night:
        # Images for given night
        images = Images.objects.extra(select={'filter_string':'get_filter_name(filter)'}).order_by('-time').filter(night=night)
        query_type = 'night'
    else:
        images = Images.objects.extra(select={'filter_string':'get_filter_name(filter)'}).order_by('-time')
        query_type = 'all'

    if type and type != 'all':
        images = images.filter(type=type)
    else:
        type = 'all'

    if request.method == 'GET':
        if request.GET.get('channel'):
            images = images.filter(channel_id=int(request.GET.get('channel')))
        if request.GET.get('filter'):
            images = images.filter(filter__name=request.GET.get('filter'))

    types = [i[0] for i in db_query("SELECT fast_distinct(%s, %s)", ('images', 'type'), simplify=False)]
    types.append('all')

    channels = [str(x) for x in range(1, settings.NCHANNELS+1)]

    return TemplateResponse(request, 'images.html', context={'images':images, 'night':night, 'type':type,
                                                             'types':types, 'query_type':query_type,
                                                             'channels':channels,
                                                             'ra':ra, 'dec':dec, 'sr':sr})

def find_image(time, type, channel_id):
    return db_query("SELECT find_image(%s, %s, %s)", (time, type, channel_id))

def find_image_id(time, type, channel_id):
    return db_query("SELECT find_image_id(%s, %s, %s)", (time, type, channel_id))

@permission_required_or_403('fweb.access_data')
def image_details(request, id=0):
    image = Images.objects.get(id=id)
    context = {'image':image}

    if image.type != 'flat' and image.type != 'dark':
        context['flat'] = find_image(image.time, 'superflat', image.channel_id)
        context['flat_id'] = find_image_id(image.time, 'superflat', image.channel_id)
        context['dark'] = find_image(image.time, 'dark', image.channel_id)
        context['dark_id'] = find_image_id(image.time, 'dark', image.channel_id)
    try:
        # Try to read original FITS keywords with comments
        filename = posixpath.join(settings.BASE, image.filename)
        filename = fix_remote_path(filename, image.channel_id)

        hdr = pyfits.getheader(filename, -1)
        ignored_keywords = ['COMMENT', 'SIMPLE', 'BZERO', 'BSCALE', 'EXTEND']
        keywords = [{'key':k, 'value':repr(hdr[k]), 'comment':hdr.comments[k]} for k in hdr.keys() if k not in ignored_keywords]

        context['keywords'] = keywords
    except:
        pass

    return TemplateResponse(request, 'image.html', context=context)

@permission_required_or_403('fweb.access_data')
def image_histogram(request, id, base=settings.BASE):
    image_obj = Images.objects.get(id=id)

    filename = Images.objects.get(id=id).filename
    filename = posixpath.join(base, filename)
    filename = fix_remote_path(filename, image_obj.channel_id)

    fits = pyfits.open(filename)
    d = fits[-1].data.flatten()

    fig = Figure(facecolor='white', figsize=(800/72, 600/72))
    ax = fig.add_subplot(111)

    if d.max() - d.min() > 10:
        ax.hist(d, bins=d.max()-d.min(), histtype='step', log=True)
    else:
        ax.hist(d, bins=1000, histtype='step', log=True)

    ax.set_xlabel("Value")
    ax.set_ylabel("")
    ax.set_title("Histogram: %s" % (filename))

    # ax.set_xlim(0, width)
    # ax.set_ylim(height, 0)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_data')
def image_slice(request, id, base=settings.BASE):
    image_obj = Images.objects.get(id=id)

    filename = Images.objects.get(id=id).filename
    filename = posixpath.join(base, filename)
    filename = fix_remote_path(filename, image_obj.channel_id)

    fits = pyfits.open(filename)

    fits = pyfits.open(posixpath.join(base, filename))
    d = fits[-1].data
    h,w = fits[-1].shape
    y0 = int(h/2)
    x0 = int(w/2)

    fig = Figure(facecolor='white', figsize=(800/72, 600/72))

    ax = fig.add_subplot(211)
    ax.plot(d[y0], drawstyle='steps')
    ax.set_xlabel("X")
    ax.set_ylabel("Value")
    ax.set_title("Horizontal and Vertical Slices at the Center")

    if not request.GET.get('nozero', 0):
        ax.set_ylim(min(0, np.min(d[y0])), np.max(d[y0]))
    ax.set_xlim(0, w)

    ax = fig.add_subplot(212)
    ax.plot(d[:,x0], drawstyle='steps')
    ax.set_xlabel("Y")
    ax.set_ylabel("Value")
    #ax.set_title("Vertical Slice")
    if not request.GET.get('nozero', 0):
        ax.set_ylim(min(0, np.min(d[:,x0])), np.max(d[:,x0]))
    ax.set_xlim(0, h)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response
