from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.core.servers.basehttp import FileWrapper
from django.shortcuts import redirect

import settings

from models import SchedulerTargets, SchedulerTargetStatus, Images

from django import forms

from django.views.decorators.cache import cache_page

from resolve import resolve
from utils import db_query, fix_remote_path, permission_required_or_403

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from mpl_toolkits.basemap import Basemap

import numpy as np
import ephem
from math import *
import os, posixpath

from tycho2 import tycho2
from coadd import coadd_rgb
from gcn_client import plot_LVC_map
from webchannel_plots import beholder_plot_status

@permission_required_or_403('fweb.access_scheduler')
def scheduler_targets_list(request, type=None):
    message = None

    inactive = False
    active = True
    completed = True
    archived = False
    failed = False

    if request.method == 'POST':
        message = ""
        ids = [int(id) for id in request.POST.getlist('target_ids[]')]
        action = request.POST.get('action')

        # message += request.POST.__str__()
        # if message:
        #     message += "<br>"

        if action == 'new':
            name = request.POST.get('new-name')
            coords = request.POST.get('new-coords')
            type = request.POST.get('new-type')
            filter = request.POST.get('new-filter')

            # Parse exposure
            try:
                exposure = float(request.POST.get('new-exposure'))

                exposure = max(0.1, min(exposure, 10000.0))
            except:
                exposure = 10.0

            # Repeat
            try:
                repeat = int(request.POST.get('new-repeat'))
                repeat = max(1, min(repeat, 1000))
            except:
                repeat = 1

            # Remove whitespaces
            name = '_'.join(name.split())

            # Parse coordinates
            sname, ra, dec = resolve(coords)

            if not sname:
                message = "Can't resolve object: %s" % (coords)
            elif not name:
                message = "Empty target name"
            else:
                id = db_query("INSERT INTO scheduler_targets (name, type, ra, dec, exposure, repeat, filter, status, time_created)"+
                              " VALUES (%s, %s, %s, %s, %s, %s, %s, get_scheduler_target_status_id('active'), favor2_timestamp()) RETURNING id",
                              (name, type, ra, dec, exposure, repeat, filter))

                message = "Target %d created: Name: %s RA: %g Dec: %g Type: %s Filter: %s Exp: %g Repeat: %d" % (id, name, ra, dec, type, filter, exposure, repeat)

        elif len(ids):
            for id in ids:
                target = SchedulerTargets.objects.get(id=id)

                if action == 'disable' and target.status.name == 'active':
                    target.status = SchedulerTargetStatus.objects.get(name='inactive')
                    target.save()
                    message += "Target %d disabled<br>" % id

                if action == 'enable' and target.status.name == 'inactive':
                    target.status = SchedulerTargetStatus.objects.get(name='active')
                    target.save()
                    message += "Target %d enabled<br>" % id

                if action == 'complete' and target.status.name != 'archived':
                    target.status = SchedulerTargetStatus.objects.get(name='completed')
                    target.save()
                    message += "Target %d completed<br>" % id

                if action == 'restart':
                    target.status = SchedulerTargetStatus.objects.get(name='active')
                    target.save()
                    message += "Target %d restarted<br>" % id

                if action == 'delete':
                    target.delete()
                    message += "Target %d deleted<br>" % id
        else:
            message = "No targets selected"

        # Redirect to the same view, but with no POST args. We can't display messages with it!
        return redirect('scheduler_targets')
    elif request.method == 'GET':
        active = int(request.GET.get('active', active))
        inactive = int(request.GET.get('inactive', inactive))
        completed = int(request.GET.get('completed', completed))
        failed = int(request.GET.get('failed', failed))

        type = request.GET.get('type', type)

    targets = SchedulerTargets.objects.order_by('-time_created')

    if not active:
        targets = targets.exclude(status__name='active')

    if not inactive:
        targets = targets.exclude(status__name='inactive')

    if not completed:
        targets = targets.exclude(status__name='completed')

    if not archived:
        targets = targets.exclude(status__name='archived')

    if not failed:
        targets = targets.exclude(status__name='failed')

    if type and type != 'all':
        targets = targets.filter(type=type)
    else:
        type = 'all'

    context = {'targets': targets, 'message': message, 'type': type,
               'active': active, 'inactive': inactive, 'completed': completed, 'archived': archived, 'failed': failed}

    context['types'] = [_['type'] for _ in SchedulerTargets.objects.distinct('type').values('type')]
    context['types'].append('all')

    return TemplateResponse(request, 'scheduler_targets.html', context=context)

@permission_required_or_403('fweb.access_scheduler')
def scheduler_target_view(request, id=0):
    target = SchedulerTargets.objects.get(id=id)
    context = {'target':target}

    if request.method == 'POST':
        action = request.POST.get('action')

        if action == 'disable' and target.status.name == 'active':
            target.status = SchedulerTargetStatus.objects.get(name='inactive')
            target.save()

        if action == 'enable' and target.status.name == 'inactive':
            target.status = SchedulerTargetStatus.objects.get(name='active')
            target.save()

        if action == 'complete' and target.status.name != 'archived':
            target.status = SchedulerTargetStatus.objects.get(name='completed')
            target.save()

        if action == 'restart':
            target.status = SchedulerTargetStatus.objects.get(name='active')
            target.save()

        if action == 'delete':
            target.delete()

        if action == 'update':
            name = request.POST.get('new-name')
            coords = request.POST.get('new-coords')
            type = request.POST.get('new-type')
            filter = request.POST.get('new-filter')

            # Parse exposure
            try:
                exposure = float(request.POST.get('new-exposure'))

                exposure = max(0.1, min(exposure, 10000.0))
            except:
                exposure = 10.0

            # Repeat
            try:
                repeat = int(request.POST.get('new-repeat'))
                repeat = max(1, min(repeat, 1000))
            except:
                repeat = 1

            # Remove whitespaces
            name = '_'.join(name.split())

            # Parse coordinates
            sname, ra, dec = resolve(coords)

            if not sname:
                message = "Can't resolve object: %s" % (coords)
            elif not name:
                message = "Empty target name"
            else:
                target.name = name
                target.type = type
                target.filter = filter
                target.exposure = exposure
                target.repeat = repeat
                target.ra = ra
                target.dec = dec
                target.save()

        return redirect('scheduler_target', id)

    uuid = target.uuid

    images = Images.objects.raw("select *,get_filter_name(filter) as filter_string from images where keywords->'TARGET UUID'='%s' and type != 'avg' order by time desc" % (uuid))
    nimages = len(list(images))

    context['images'] = images
    context['nimages'] = nimages

    return TemplateResponse(request, 'scheduler_target.html', context=context)

@permission_required_or_403('fweb.access_scheduler')
def scheduler_target_status(request, id=0):
    target = SchedulerTargets.objects.get(id=id)

    extra = []
    if target.type == 'Swift' or target.type == 'Fermi':
        if target.params.has_key('error'):
            err = float(target.params['error'])
        else:
            err = 1.0

        extra = [{'type':'circle', 'ra':target.ra, 'dec':target.dec, 'sr':max(1.0, err), 'color':'darkcyan', 'lw':3}]

    response = HttpResponse(content_type='image/png')
    beholder_plot_status(response, time=target.time_created, extra=extra)

    print extra

    return response

def deg2rad(x):
    return 3.14159265359*x/180.0

def rad2deg(x):
    return 180.0*x/3.14159265359

@permission_required_or_403('fweb.access_scheduler')
@cache_page(30)
def scheduler_map(request, size=600):
    targets = SchedulerTargets.objects.order_by('-time_created').exclude(ra__isnull=True)

    # SAO location
    obs = ephem.Observer()
    obs.lat = deg2rad(settings.LATITUDE)
    obs.lon = deg2rad(settings.LONGITUDE)
    obs.elevation = settings.ELEVATION
    obs.pressure = 0.0

    # Current zenith position
    z = obs.radec_of(0, 90)
    st = rad2deg(z[0]) # stellar time in degrees

    fig = Figure(facecolor='white', dpi=72, figsize=(size/72, size/72), tight_layout=True)
    ax = fig.add_subplot(111)

    # set up orthographic map projection
    map = Basemap(projection='ortho', lat_0 = rad2deg(obs.lat), lon_0 = -st, resolution = None, celestial = True, ax = ax)
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
    map.scatter(px, py, s=star_size, c='gray')

    # Sun
    s = ephem.Sun()
    s.compute(obs)
    px, py = map(rad2deg(s.ra), rad2deg(s.dec))
    if abs(px) < 1e10 and abs(py) < 1e10:
        map.scatter(px, py, s=2000, c='lightgray', marker='o')
        ax.text(px, py, "Sun", color='black', va='center', ha='center')

    # Moon
    m = ephem.Moon()
    m.compute(obs)
    px, py = map(rad2deg(m.ra), rad2deg(m.dec))
    if abs(px) < 1e10 and abs(py) < 1e10:
        map.scatter(px, py, s=1200, c='lightgray', marker='o')
        ax.text(px, py, "Moon", color='black', va='center', ha='center')

    mp, ip, cp = None, None, None
    for target in targets:
        if target.status.name == 'completed' or target.status.name == 'archived' or target.status.name == 'inactive' or target.status.name == 'failed':
            continue

        px, py = map(target.ra, target.dec)
        if abs(px) < 1e10 and abs(py) < 1e10:
            if target.type == 'monitoring':
                mp = map.scatter(px, py, marker='o', s=300, alpha=0.3, color='blue')
            elif target.type == 'imaging':
                ip = map.scatter(px, py, marker='o', s=300, alpha=0.3, color='green')
            else:
                cp = map.scatter(px, py, marker='o', s=300, alpha=0.3, color='yellow')

            ax.text(px, py, target.name, color='black', va='bottom', ha='left')

    # Title etc
    ax.set_title("%s UT, Sidereal %s" % (obs.date, (ephem.hours(deg2rad(st)))))

    if cp or mp or ip:
        fig.legend([mp, ip, cp], ["Monitoring", "Imaging", "Custom"], loc=4).draw_frame(False)

    # Return the image
    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response, bbox_inches='tight')

    return response

@permission_required_or_403('fweb.access_scheduler')
def scheduler_target_rgb_image(request, id=0):
    base = posixpath.join("/tmp/fweb/targets", str(id))

    try:
        os.makedirs(base)
    except:
        pass

    rgbname = posixpath.join(base, "rgb.jpg")

    if not posixpath.isfile(rgbname):
        # No cached file
        try:
            target = SchedulerTargets.objects.get(id=id)
            images = Images.objects.raw("select *,get_filter_name(filter) as filter_string from images where keywords->'TARGET UUID'='%s' and q3c_radial_query(ra0, dec0, %s, %s, 2.0) order by channel_id" % (target.uuid, target.ra, target.dec))

            files_b, files_v, files_r = [], [], []

            for image in images:
                filename = posixpath.join(settings.BASE, image.filename)
                filename = fix_remote_path(filename, image.channel_id)

                if image.filter_string == 'B':
                    files_b.append(filename)
                if image.filter_string == 'V':
                    files_v.append(filename)
                if image.filter_string == 'R':
                    files_r.append(filename)

            if len(files_b) and len(files_v) and len(files_r):
                print files_b[0], files_v[0], files_r[0]
                coadd_rgb(name_blue=files_b[0], name_green=files_v[0], name_red=files_r[0], out=rgbname)
        except:
            pass

    if not posixpath.isfile(rgbname):
        return HttpResponse("Can't create RGB image for target %s" % str(id))

    response = HttpResponse(FileWrapper(file(rgbname)), content_type='image/jpeg')
    response['Content-Length'] = posixpath.getsize(rgbname)
    return response

# @permission_required_or_403('fweb.access_scheduler')
def scheduler_target_lvc_image(request, id=0):
    target = SchedulerTargets.objects.get(id=id)

    # dirname = posixpath.join("TRIGGERS", "LVC", str(target.external_id))
    # imagename = posixpath.join(dirname, "map.png")
    # if not posixpath.exists(imagename) and False:
    #     plot_LVC_map(target.id, file=imagename)

    response = HttpResponse(content_type='image/png')
    #response['Content-Length'] = posixpath.getsize(imagename)
    plot_LVC_map(target.id, file=response)
    return response
