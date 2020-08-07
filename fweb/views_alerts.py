from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect

from models import Alerts, Images, RealtimeObjects

import settings

from utils import permission_required_or_403, assert_permission, fix_remote_path, db_query

import astropy.io.fits as pyfits
import astropy.wcs as pywcs # for all_world2pix, which is absent from original pywcs
import numpy as np

import ephem

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from mpl_toolkits.basemap import Basemap

import matplotlib.pyplot as plt
from tycho2 import tycho2

from favor2 import get_time_from_night

#@permission_required_or_403('fweb.access_data')
def alerts_list(request, night='all'):
    context = {}

    alerts = Alerts.objects.order_by('-time')

    if night and night != 'all':
        alerts = alerts.filter(night=night)

    for alert in alerts:
        obj = RealtimeObjects.objects.filter(channel_id=alert.channel_id).filter(time_start=alert.time).first()
        if obj:
            alert.obj = obj

            r = obj.realtimerecords_set.order_by('time').first()
            if r:
                i = obj.realtimeimages_set.filter(time=r.time).first()
                if i:
                    alert.firstimage = i

        alert.nimages = Images.objects.filter(type='alert').extra(where=["keywords->'TARGET'='alert_%s'" % alert.id]).count()

    context['alerts'] = alerts
    context['night'] = night

    context['night_prev'] = Alerts.objects.distinct('night').filter(night__lt=night).order_by('-night').values('night').first()
    context['night_next'] = Alerts.objects.distinct('night').filter(night__gt=night).order_by('night').values('night').first()

    return TemplateResponse(request, 'alerts_list.html', context=context)

#@permission_required_or_403('fweb.access_data')
def alert_details(request, id=0):
    context = {}

    alert = Alerts.objects.get(id=id)
    context['alert'] = alert

    images = Images.objects.filter(type='alert').order_by('time')
    images = images.extra(where=["keywords->'TARGET'='alert_%s'" % alert.id])
    context['images'] = images

    context['sr'] = 0.4

    channels = {}
    exposures = {}
    filters = {}
    dt = None

    channel_ids = [_ for _ in xrange(1, settings.NCHANNELS+1)]

    for image in images:
        # if image.channel_id != 2:
        #     continue
        if not channels.has_key(image.channel_id):
            channels[image.channel_id] = []

        channels[image.channel_id].append(image)
        exposures[image.channel_id] = image.keywords['EXPOSURE']
        filters[image.channel_id] = image.filter.name if image.filter else 'Custom'

        if dt is None:
            dt = (image.time - alert.time).total_seconds()
            context['dt'] = dt

    context['channels'] = channels
    context['channel_ids'] = channel_ids
    context['filters'] = filters
    context['exposures'] = exposures

    obj = RealtimeObjects.objects.filter(channel_id=alert.channel_id).filter(time_start=alert.time).first()
    context['object'] = obj

    return TemplateResponse(request, 'alert_details.html', context=context)

@permission_required_or_403('fweb.access_data')
def alerts_map(request, night='', state='all', size=800):
    alerts = Alerts.objects.order_by('time')
    time = None

    if night and night != 'all':
        alerts = alerts.filter(night=night)
        time = get_time_from_night(night)

    ra,dec=[],[]
    N = 0
    N0 = 0
    Nf = 0

    for alert in alerts:
        ra.append(alert.ra)
        dec.append(alert.dec)

        N0 += 1
        if RealtimeObjects.objects.filter(channel_id=alert.channel_id).filter(time_start=alert.time).exists():
            N += 1

        if Images.objects.filter(type='alert').extra(where=["keywords->'TARGET'='alert_%s'" % alert.id]).exists():
            Nf += 1

    # Map
    fig = Figure(facecolor='white', dpi=72, figsize=(size/72, 0.6*size/72), tight_layout=True)
    ax = fig.add_subplot(111)

    # set up gnomonic map projection
    map = Basemap(projection='hammer', lat_0 = 0, lon_0 = 0, resolution = None, celestial = True, ax = ax)
    # draw the edge of the map projection region (the projection limb)
    map.drawmapboundary()

    # draw and label ra/dec grid lines every 30 degrees.
    degtoralabel = lambda deg : "$%d^h$" % int(deg/15)
    degtodeclabel = lambda deg : "%+d$^\circ$" % deg
    map.drawparallels(np.arange(-90, 90, 30), color='lightgray')#, fmt=degtodeclabel)
    map.drawmeridians(np.arange(0, 360, 45), color='lightgray')

    for h in [0,3,6,9,12,15,18,21]:
        try:
            x,y = map(h*15,0)
            if abs(x) < 1e10 and abs(y) < 1e10:
                ax.text(x,y, degtoralabel(h*15), color='black')
        except:
            pass

    # Place stars
    star_size = [10*(10**(-0.4*v)) for v in tycho2['v']]
    px, py = map(tycho2['ra'], tycho2['dec'])
    map.scatter(px, py, s=star_size, c='gray', edgecolors='None')

    # Sun and Moon
    if time is not None:
        # SAO location
        obs = ephem.Observer()
        obs.lat = np.deg2rad(settings.LATITUDE)
        obs.lon = np.deg2rad(settings.LONGITUDE)
        obs.elevation = settings.ELEVATION
        obs.pressure = 0.0
        obs.date = time

        s = ephem.Sun()
        s.compute(obs)
        px, py = map(np.rad2deg(s.ra), np.rad2deg(s.dec))
        if abs(px) < 1e10 and abs(py) < 1e10:
            map.scatter(px, py, s=2000, c='lightgray', marker='o')
            ax.text(px, py, "Sun", color='black', va='center', ha='center')

        # anti-Sun
        as_ra = (np.rad2deg(s.ra) + 180) % 360
        as_dec = -np.rad2deg(s.dec)
        px,py = map(as_ra, as_dec)
        if abs(px) < 1e10 and abs(py) < 1e10:
            map.scatter(px, py, s=2000, c='lightgray', marker='o', linestyle='--', alpha=0.5)
            ax.text(px, py, "Anti\nSun", color='black', va='center', ha='center', alpha=0.5)

        m = ephem.Moon()
        m.compute(obs)
        px, py = map(np.rad2deg(m.ra), np.rad2deg(m.dec))
        if abs(px) < 1e10 and abs(py) < 1e10:
            map.scatter(px, py, s=1200, c='lightgray', marker='o')
            ax.text(px, py, "Moon", color='black', va='center', ha='center')

    # Alerts
    if alerts:
        px, py = map(ra, dec)
        map.scatter(px, py, s=100, c='red', marker='*', edgecolors='blue', linewidths=0.3)

    # Title
    if night and night != 'all':
        fig.suptitle("Realtime Alerts for night %s" % night)
    else:
        fig.suptitle("Realtime Alerts")

    ax.set_title("%d alerts, %d with objects, %d followed up" % (N0, N, Nf))

    # Return the image
    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response, bbox_inches='tight')

    return response
