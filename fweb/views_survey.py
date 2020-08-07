from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect

from models import Records, Images, SurveyTransients

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.ticker import ScalarFormatter
from matplotlib.patches import Ellipse

from resolve import resolve

import settings

from utils import permission_required_or_403, assert_permission, fix_remote_path, db_query

import astropy.io.fits as pyfits
import astropy.wcs as pywcs # for all_world2pix, which is absent from original pywcs
import fitsimage

import StringIO, base64, datetime

import numpy as np

import survey as survey
from griddata import griddata

from views_images import find_image
import os,sys,posixpath,glob,datetime

from favor2 import Favor2

favor2_filters = {0:'gray', 1:'b', 2:'g', 3:'r', 4:'gray', 5:'b', 6:'g', 7:'r'}

def check_pos(header, ra, dec):
    header = dict(header)
    for key in header.keys():
        try:
            header[key] = float(header[key])
        except:
            pass

    wcs = pywcs.WCS(header=header)
    x, y = wcs.all_world2pix(ra, dec, 0)

    if x > 10 and y > 10 and x < settings.IMAGE_WIDTH - 10 and y < settings.IMAGE_HEIGHT:
        return True
    else:
        return False

#@permission_required_or_403('fweb.access_data')
def survey_info(request, ra=0, dec=0, sr=0.1, filter=True, imtype='survey'):
    if request.method == 'POST':
        coords = request.POST.get('coords')
        sr = request.POST.get('sr')
        sr = float(sr) if sr else 0.01
        name, ra, dec = resolve(coords)

        return redirect('/survey/%g/%g/%g' % (ra, dec, sr))

    if request.GET.has_key('type'):
        imtype = request.GET.get('type', 'survey')

    context = {}

    context['ra'] = ra
    context['dec'] = dec
    context['sr'] = sr

    # TODO: expand the search area a bit and filter images containing the point
    images = Images.objects.extra(where=["q3c_radial_query(ra0, dec0, %s, %s, %s)"],
                                  params = (ra, dec, 7.0),
                                  select={'filter_string':'get_filter_name(filter)'}).filter(type=imtype).order_by('-time')

    if request.method == 'GET' and request.GET.get('channel_id'):
        images = images.filter(channel_id = int(request.GET.get('channel_id')))

    if request.method == 'GET':
        filter = int(request.GET.get('filter', 1))

    if filter and filter==1:
        images = [i for i in images if check_pos(i.keywords, float(ra), float(dec))]

    if filter and filter==2:
        images = images.extra(where=["id IN (SELECT DISTINCT frame FROM records2 WHERE q3c_radial_query(ra, dec, %s, %s, %s))"],
                              params = (ra, dec, 0.01))

    context['images'] = images

    return TemplateResponse(request, 'survey_info.html', context=context)

def crop_image(image0, x0, y0, size):
    h, w = image0.shape

    x1 = max(0, min(x0 - size, w - 1))
    y1 = max(0, min(y0 - size, h - 1))
    x2 = min(w - 1, max(x0 + size, 0))
    y2 = min(h - 1, max(y0 + size, 0))

    return image0[y1:y2, x1:x2], x1, y1

#@permission_required_or_403('fweb.access_data')
def survey_image(request, id=0, ra=0, dec=0, sr=0.1, size=300, plot_objects=True):
    image = Images.objects.get(id=id)
    filename = image.filename
    filename = fix_remote_path(filename, image.channel_id)

    imdata = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    wcs = pywcs.WCS(header)

    x, y = wcs.all_world2pix(float(ra), float(dec), 0)
    pixscale = np.hypot(wcs.wcs.cd[0, 0], wcs.wcs.cd[0, 1])
    cutsize = max(10, float(sr)/pixscale)
    #imdata2,x0,y0 = crop_image(imdata, x, y, cutsize)

    if plot_objects:
        objects = survey.get_objects_from_file(filename)
    else:
        objects = None

    fig = Figure(facecolor='white', dpi=72, figsize=(size*1.0/72, size*1.0/72))
    ax = fig.add_axes([0,0,1,1])
    ax.axis('off')

    # if x > -cutsize and x <= imdata.shape[1] + cutsize and y > -cutsize and y <= imdata.shape[0] + cutsize:
    #     limits = np.percentile(imdata2, [0.5, 99.0])
    #     ax.imshow(imdata2, cmap='hot', origin='lower', interpolation='nearest', vmin=limits[0], vmax=limits[1])
    #     if x < cutsize:
    #         ax.set_xlim(x - cutsize, x + cutsize - 1)
    #     if x >= imdata.shape[1] - cutsize:
    #         ax.set_xlim(0, 2*cutsize - 1)
    #     if y < cutsize:
    #         ax.set_ylim(y - cutsize, y + cutsize - 1)
    #     if y >= imdata.shape[0] - cutsize:
    #         ax.set_ylim(0, 2*cutsize - 1)

    y1,x1 = np.mgrid[0:imdata.shape[0],0:imdata.shape[1]]
    idx = (x1 >= x - cutsize) & (x1 <= x + cutsize) & (y1 >= y - cutsize) & (y1 <= y + cutsize)
    idx = idx & (x1 > 0) & (y1 > 0) & (x1 < imdata.shape[1]-1) & (y1 < imdata.shape[0]-1)

    if len(imdata[idx]):
        limits = np.percentile(imdata[idx], [0.5, 99.0])
        ax.imshow(imdata, cmap='hot', origin='lower', interpolation='nearest', vmin=limits[0], vmax=limits[1])
        ax.set_xlim(x - cutsize, x + cutsize)
        ax.set_ylim(y - cutsize, y + cutsize)

        if objects:
            ax.autoscale(False)
            ax.scatter(objects['x'], objects['y'], c='blue')

    if request.method == 'GET':
        if request.GET.has_key('ra0') and request.GET.has_key('dec0'):
            ra0 = float(request.GET.get('ra0', 0))
            dec0 = float(request.GET.get('dec0', 0))
            sr0 = float(request.GET.get('sr0', 0.01))

            x0, y0 = wcs.all_world2pix(float(ra0), float(dec0), 0)
            psr0 = float(sr0)/pixscale

            ax.add_patch(Ellipse(xy=[x0,y0], width=2*psr0, height=2*psr0, edgecolor='green', fc='None', lw=2))

    response = HttpResponse(content_type="image/png")

    canvas = FigureCanvas(fig)
    canvas.print_png(response, bbox_inches='tight')

    return response

import subprocess, os

#@permission_required_or_403('fweb.access_data')
def survey_image_download(request, id=0, ra=0, dec=0, sr=0.1, size=200, plot_objects=True):
    image = Images.objects.get(id=id)
    filename = image.filename
    filename = fix_remote_path(filename, image.channel_id)

    header = pyfits.getheader(filename, -1)

    wcs = pywcs.WCS(header)

    x, y = wcs.all_world2pix(float(ra), float(dec), 0)
    pixscale = np.hypot(wcs.wcs.cd[0, 0], wcs.wcs.cd[0, 1])
    cutsize = max(10, float(sr)/pixscale)

    process = subprocess.Popen(["./crop", filename, "-", "x0=%d" % np.round(x), "y0=%d" % np.round(y), "width=%d" % np.round(2*cutsize), "height=%d" % np.round(2*cutsize)], stdout=subprocess.PIPE)
    content = process.communicate()[0]

    response = HttpResponse(content, content_type='application/octet-stream')
    response['Content-Disposition'] = 'attachment; filename=crop_'+os.path.split(filename)[-1]
    response['Content-Length'] = len(content)
    return response

#@permission_required_or_403('fweb.access_data')
def survey_transients_list(request, night='all', ra=0, dec=0, sr=0.1):
    if request.method == 'POST':
        return redirect('/survey/transients')

    context = {}

    transients = SurveyTransients.objects.order_by('-time')

    if night and night != 'all':
        transients = transients.filter(night=night)
        context['night'] = night

    if request.method == 'GET':
        if request.GET.get('mpc'):
            transients = transients.filter(mpc__isnull = False)

        if request.GET.get('reliable'):
            transients = transients.extra(where=["(params->'reliable') = '1'"])

        if request.GET.get('min_sn'):
            min_sn = float(request.GET.get('min_sn'))
            transients = transients.extra(where=["flux/flux_err > %s"], params=[min_sn])

        if request.GET.get('noident'):
            transients = transients.filter(mpc__isnull = True, simbad__isnull = True)


    context['transients'] = transients

    return TemplateResponse(request, 'survey_transients.html', context=context)

def survey_transient_image(request, id=0):
    transient = SurveyTransients.objects.get(id=id)

    content = base64.decodestring(transient.preview[22:])

    response = HttpResponse(content, content_type='image/png')
    response['Content-Length'] = len(content)

    return response

def survey_transient_details(request, id=0, days=2, sr=0.2):
    transient = SurveyTransients.objects.get(id=id)

    context = {}

    if request.method == 'GET':
        days = float(request.GET.get('days', days))
        sr = float(request.GET.get('sr', sr))

        context['days'] = days
        context['sr'] = sr

    context['transient'] = transient

    images = Images.objects.extra(where=["q3c_radial_query(ra0, dec0, %s, %s, %s)"],
                                  params = (transient.ra, transient.dec, 7.0),
                                  select={'filter_string':'get_filter_name(filter)'}).order_by('-time')

    if transient.frame.type != 'survey':
        images = images.extra(where=["type='survey' or type=%s"], params=(transient.frame.type,))
    else:
        images = images.filter(type='survey')

    images = images.filter(time__gte = transient.time - datetime.timedelta(days=days))
    images = images.filter(time__lte = transient.time + datetime.timedelta(days=days))

    images = [i for i in images if check_pos(i.keywords, transient.ra, transient.dec)]

    context['images'] = images

    return TemplateResponse(request, 'survey_transient.html', context=context)


def radectoxieta(ra, dec, ra0=0, dec0=0):
    ra,dec = [np.asarray(_) for _ in ra,dec]
    delta_ra = np.asarray(ra - ra0)

    delta_ra[(ra < 10) & (ra0 > 350)] += 360
    delta_ra[(ra > 350) & (ra0 < 10)] -= 360

    xx = np.cos(dec*np.pi/180)*np.sin(delta_ra*np.pi/180)
    yy = np.sin(dec0*np.pi/180)*np.sin(dec*np.pi/180) + np.cos(dec0*np.pi/180)*np.cos(dec*np.pi/180)*np.cos(delta_ra*np.pi/180)
    xi = (xx/yy)

    xx = np.cos(dec0*np.pi/180)*np.sin(dec*np.pi/180) - np.sin(dec0*np.pi/180)*np.cos(dec*np.pi/180)*np.cos(delta_ra*np.pi/180)
    eta = (xx/yy)

    xi *= 180./np.pi
    eta *= 180./np.pi

    return xi,eta

#@permission_required_or_403('fweb.access_data')
def survey_lightcurve(request, ra=0, dec=0, sr=15.0/3600, size=900, quality=0.0, plot_map=True):
    ra = float(ra)
    dec = float(dec)
    sr = float(sr) if sr else 15.0/3600
    P = float(request.GET.get('P')) if request.GET.get('P') else 0

    records = Records.objects.extra(where=["q3c_radial_query(ra, dec, %s, %s, %s)"],
                                    params = (ra, dec, 2.8*sr),
                                    select={'dr':'q3c_dist(ra,dec,%g,%g)' % (ra, dec)}).filter(filter=0).order_by('time')

    if quality and quality > 0:
        records = records.filter(quality__lt = quality)

    t,v,cbv,cvr,ra1,dec1,dr = [],[],[],[],[],[],[]
    C = [0,0,0]

    for r in records:
        if r.dr < sr:
            t.append(r.time)
            v.append(r.mag)
            cbv.append(r.cbv)
            cvr.append(r.cvr)
        ra1.append(r.ra)
        dec1.append(r.dec)
        dr.append(r.dr)

    t,v,cbv,cvr,ra1,dec1,dr = [np.array(_) for _ in t,v,cbv,cvr,ra1,dec1,dr]

    if request.GET.has_key('bv'):
        C = [0, -float(request.GET.get('bv')), -float(request.GET.get('vr'))]
    elif len(v) > 10:
        X = np.vstack([np.ones(len(cvr)), cbv, cvr]).T
        Y = v
        idx = np.ones_like(Y, dtype=np.bool)

        for i in xrange(3):
            C = np.linalg.lstsq(X[idx], Y[idx])[0]
            YY = np.sum(X*C,axis=1)
            idx = np.abs(Y-YY) < 2.0*np.std(Y-YY)
            print C, np.std(Y-YY),np.std((Y-YY)[idx])

    vv = v - C[1]*cbv - C[2]*cvr

    if request.GET.has_key('filter'):
        A = float(request.GET.get('filter'))

        idx = np.abs(vv - np.mean(vv)) < A*np.std(vv)
        t,v,vv = [_[idx] for _ in t,v,vv]

    dpi = 72
    figsize = (size/dpi, size*0.6/dpi)

    fig = Figure(facecolor='white', figsize=figsize, tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()

    if P > 0:
        dt = np.array([(_ - t[0]).total_seconds() for _ in t])
        ax.scatter((dt % (24.0*3600.0*P))/(24.0*3600.0*P), vv, marker='.')
        ax.scatter((dt % (24.0*3600.0*P))/(24.0*3600.0*P) - 1, vv, marker='.')
        ax.scatter((dt % (24.0*3600.0*P))/(24.0*3600.0*P) + 1, vv, marker='.')

        ax.set_xlim([-0.4, 1.4])

        ax.axvline(0, c='gray', alpha=0.5)
        ax.axvline(1, c='gray', alpha=0.5)

        ax.set_xlabel('Phase, P = %g days' % P)
    else:
        ax.scatter(t, vv, marker='.')

        if records: # It is failing if no points are plotted
            ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d %H:%M:%S'))

        ax.set_xlabel("Time, UT")

    ax.set_ylabel("Magnitude")
    ax.invert_yaxis()

    ax.set_title("RA=%g Dec=%g SR=%.2g B-V=%.2f V-R=%.2f  -  V*=%.2f+/-%.2f" % (ra, dec, sr, -C[1], -C[2], np.mean(vv), np.std(vv)))

    fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    if plot_map:
        # Inlet plot with the map
        ax2 = fig.add_axes([0.83, 0.79, 0.2, 0.2], aspect=1.0, frameon=True)
        ax2.set_xlim(-2.0*sr, 2.0*sr)
        ax2.set_ylim(-2.0*sr, 2.0*sr)
        ax2.xaxis.set_ticks([])
        ax2.yaxis.set_ticks([])
        xi,eta = radectoxieta(ra1,dec1,ra0=ra,dec0=dec)
        ax2.plot(xi, eta, '.', color='green')
        ax2.add_patch(Ellipse(xy=[0,0], width=2*sr, height=2*sr, edgecolor='blue', fc='None'))

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

#@permission_required_or_403('fweb.access_data')
def survey_photometry(request, imtype='survey'):
    context = {}

    ra,dec,sr = None,None,0.01

    if request.method == 'POST':
        coords = request.POST.get('coords')
        sr = request.POST.get('sr')
        sr = float(sr) if sr else 0.01

        context['coords'] = coords

        name, ra, dec = resolve(coords)
        context['name'] = name

        if request.POST.get('bv'):
            context['bv'] = request.POST.get('bv')

        if request.POST.get('vr'):
            context['vr'] = request.POST.get('vr')

        if request.POST.get('period'):
            context['period'] = request.POST.get('period')

    context['ra'] = ra
    context['dec'] = dec

    context['sr'] = sr

    if ra is not None and dec is not None:
        record = Records.objects.extra(where=["q3c_radial_query(ra, dec, %s, %s, %s)"],
                                       params = (ra, dec, sr)).filter(filter=0).order_by('quality').first()
        if record:
            context['best_image'] = record.frame.id

    return TemplateResponse(request, 'survey_photometry.html', context=context)

#@permission_required_or_403('fweb.access_data')
def survey_dbg_image(request, id=0, size=900, cat='pickles'):
    if request.method == 'GET':
        if request.GET.has_key('cat'):
            cat = request.GET.get('cat')

    image_obj = Images.objects.get(id=id)

    filename = fix_remote_path(image_obj.filename, image_obj.channel_id)

    darkname = find_image(image_obj.time, 'dark', image_obj.channel_id)
    darkname = fix_remote_path(darkname, image_obj.channel_id)

    flatname = find_image(image_obj.time, 'flat', image_obj.channel_id)
    flatname = fix_remote_path(flatname, image_obj.channel_id)

    data = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    # Prepare
    if posixpath.exists(darkname):
        dark = pyfits.getdata(darkname, -1)
        data -= dark

        if posixpath.exists(flatname):
            flat = pyfits.getdata(flatname, -1)
            data *= np.mean(flat)/flat

    # Calibrate
    obj = survey.get_objects_from_file(filename, simple=False, filter=True)
    wcs = pywcs.WCS(header)
    ra0, dec0, sr0 = survey.get_frame_center(header=header, wcs=wcs)

    print filename,'-',ra0,dec0,sr0

    favor2 = Favor2()
    if cat == 'apass':
        cat = favor2.get_stars(ra0, dec0, sr0, catalog='apass', limit=600000, extra=["g<14 and g>8.5", "b<20 and v<20 and r<20 and i<20 and g<20", "var = 0", "gerr>0 and rerr>0 and ierr>0"])
    else:
        cat = favor2.get_stars(ra0, dec0, sr0, catalog='pickles', limit=200000, extra=["var = 0", "rank=1"])

    print "%d objects, %d stars" % (len(obj['ra']), len(cat['ra']))

    survey.fix_distortion(obj, cat, header=header)

    Y,YY,corr,oidx = survey.calibrate_objects(obj, cat, sr=10./3600, retval='all')

    # Plot
    dpi = 72
    figsize = (size/dpi, size*4*0.6/dpi)
    fig = Figure(facecolor='white', figsize=figsize, tight_layout=True)

    # 1
    ax = fig.add_subplot(411)
    limits = np.percentile(data, [0.5, 99.5])
    i=ax.imshow(data, origin='lower', cmap='hot', interpolation='nearest', vmin=limits[0], vmax=limits[1])
    fig.colorbar(i)
    ax.set_title("%d / %s / ch=%d at %s" % (image_obj.id, image_obj.type, image_obj.channel_id, image_obj.time))

    # 2
    ax = fig.add_subplot(412)

    gmag0, bins, binloc = griddata(obj['x'][oidx], obj['y'][oidx], Y, binsize=100)
    limits = np.percentile(gmag0[np.isfinite(gmag0)], [0.5, 95.5])

    i=ax.imshow(gmag0, origin='lower', extent=[0, header['NAXIS1'], 0, header['NAXIS2']], interpolation='nearest', vmin=limits[0], vmax=limits[1])
    fig.colorbar(i)
    ax.autoscale(False)
    ax.plot(obj['x'][oidx], obj['y'][oidx], 'b.', alpha=0.3)
    ax.set_title("Actual zero point")

    # 3
    ax = fig.add_subplot(413)

    gmag0, bins, binloc = griddata(obj['x'][oidx], obj['y'][oidx], YY, binsize=100)
    #limits = np.percentile(gmag0[np.isfinite(gmag0)], [0.5, 95.5])

    i=ax.imshow(gmag0, origin='lower', extent=[0, header['NAXIS1'], 0, header['NAXIS2']], interpolation='nearest', vmin=limits[0], vmax=limits[1])
    fig.colorbar(i)
    ax.autoscale(False)
    ax.plot(obj['x'][oidx], obj['y'][oidx], 'b.', alpha=0.3)
    ax.set_title("Fitted zero point")

    # 4
    ax = fig.add_subplot(414)

    gmag0, bins, binloc = griddata(obj['x'][oidx], obj['y'][oidx], Y-YY, binsize=100)
    limits = np.percentile(gmag0[np.isfinite(gmag0)], [0.5, 95.5])

    i=ax.imshow(gmag0, origin='lower', extent=[0, header['NAXIS1'], 0, header['NAXIS2']], interpolation='nearest', vmin=limits[0], vmax=limits[1])
    fig.colorbar(i)
    ax.autoscale(False)
    ax.plot(obj['x'][oidx], obj['y'][oidx], 'b.', alpha=0.3)
    ax.set_title("Zero point residuals. std=%g" % np.std(Y-YY))


    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response
