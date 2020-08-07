from django.http import HttpResponse,HttpResponseNotFound,Http404
from django.template.response import TemplateResponse
from django.shortcuts import redirect
from django.db.models import Avg, Min, Max, StdDev

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.ticker import ScalarFormatter
from matplotlib.patches import Ellipse

import numpy as np
import scipy.signal as signal

import ephem
import astropy

import settings

from models import RealtimeRecords, Satellites, SatellitesView, SatelliteTracks, SatelliteRecords, SatelliteRecordsSimple

from templatetags.filters import satellite_track_start, satellite_track_end, satellite_catalogue, filter_name

from utils import permission_required_or_403, assert_permission, has_permission, db_query

#favor2_filters = ['gray', 'b', 'g', 'r', 'gray', 'b', 'g', 'r']
favor2_filters = [[0, 0, 0], [0, 0, 1], [0, 1, 0], [1, 0, 0], [0, 0, 0], [0, 0, 1], [0, 1, 0], [1, 0, 0]]

@permission_required_or_403('fweb.access_satellites')
def satellites_list(request, id=0, catalogue_id=0, name=None, comments=None, sort=None):
    context = {}

    context['should_paginate'] = True

    if request.method == 'POST':
        id = request.POST.get('id')
        catalogue_id = request.POST.get('catalogue_id')
        name = request.POST.get('name')
        comments = request.POST.get('comments')
        context['should_paginate'] = False
    elif  request.method == 'GET':
        sort = request.GET.get('sort')

    if not sort:
        sort = '-time_last'

    satellites = SatellitesView.objects.order_by(sort)
    tracks = SatelliteTracks.objects.all()

    context['nsatellites'] = satellites.count()
    context['nsatellites_str'] = "".join(["*" for i in xrange(context['nsatellites']/1000)])
    context['ntracks'] = tracks.count()
    context['time_min'] = SatelliteRecords.objects.aggregate(Min('time'))['time__min']
    context['time_max'] = SatelliteRecords.objects.aggregate(Max('time'))['time__max']

    if sort:
        nsort = sort
        if sort[0] == '-':
            nsort = sort[1:]

        if nsort in satellites.model._meta.get_all_field_names():
            satellites = satellites.extra(where=["%s is not Null" % nsort])

    if sort in ['variability_period', '-variability_period']:
        satellites = satellites.filter(variability=2)

    context['sort'] = sort
    context['sort_description'] = {
        'time_last': 'oldest last track',
        '-time_last': 'latest track',
        'time_first': 'oldest first track',
        '-time_first': 'newest first track',
        'catalogue_id': 'catalogue id',
        '-catalogue_id': 'catalogue id, descending',
        'ntracks': 'number of tracks',
        '-ntracks': 'number of tracks, descending',
        'variability_period': 'variability period',
        '-variability_period': 'variability period, descending',
        'mean_clear': 'mean brightness',
        '-mean_clear': 'mean brightness, descending',
        'rcs': 'Radar Cross-Section',
        '-rcs': 'Radar Cross-Section, descending',
    }.get(sort, sort)

    if request.method == 'POST':
        if not request.POST.get('var_nonvariable'):
            satellites = satellites.exclude(variability=0)

        if not request.POST.get('var_variable'):
            satellites = satellites.exclude(variability=1)

        if not request.POST.get('var_periodic'):
            satellites = satellites.exclude(variability=2)
        else:
            if request.POST.get('var_pmin') or request.POST.get('var_pmax'):
                satellites = satellites.filter(variability=2)

            if request.POST.get('var_pmin'):
                satellites = satellites.filter(variability_period__gt=request.POST.get('var_pmin'))
            if request.POST.get('var_pmax'):
                satellites = satellites.filter(variability_period__lt=request.POST.get('var_pmax'))

        for i in range(8):
            if not request.POST.get('type_%d' % i):
                satellites = satellites.exclude(type=i)

        if request.POST.get('orb_incl_min'):
            satellites = satellites.filter(orbit_inclination__gt=request.POST.get('orb_incl_min'))
        if request.POST.get('orb_incl_max'):
            satellites = satellites.filter(orbit_inclination__lt=request.POST.get('orb_incl_max'))
        if request.POST.get('orb_period_min'):
            satellites = satellites.filter(orbit_period__gt=request.POST.get('orb_period_min'))
        if request.POST.get('orb_period_max'):
            satellites = satellites.filter(orbit_period__lt=request.POST.get('orb_period_max'))
        if request.POST.get('orb_ecc_min'):
            satellites = satellites.filter(orbit_eccentricity__gt=request.POST.get('orb_ecc_min'))
        if request.POST.get('orb_ecc_max'):
            satellites = satellites.filter(orbit_eccentricity__gt__lt=request.POST.get('orb_ecc_max'))

    if id:
        satellites = satellites.filter(id=id)
        context['id'] = id

    if catalogue_id:
        cat_ids = [int(i) for i in catalogue_id.replace(',', ' ').split()]
        satellites = satellites.filter(catalogue_id__in=cat_ids)
        context['catalogue_id'] = catalogue_id

    if name:
        if len(name) == 9 and name[4] == '-' and satellites.filter(iname__iexact=name).exists():
            satellites = satellites.filter(iname__iexact=name)
        elif satellites.filter(name__iexact=name).exists():
            satellites = satellites.filter(name__iexact=name)
        else:
            satellites = satellites.filter(name__icontains=name)
        context['name'] = name

    if comments:
        satellites = satellites.filter(comments__icontains=comments)

    if not has_permission(request, 'fweb.access_satellites_all'):
        satellites = satellites.exclude(catalogue=2)
        satellites = satellites.exclude(country='CIS',country__isnull=False)

    if request.method == 'POST' and satellites.count() == 1:
        return redirect('satellite_details', id=satellites.first().id)

    context['satellites'] = satellites
    context['tracks'] = tracks

    return TemplateResponse(request, 'satellites.html', context=context)

def parse_tle(tle):
    s = tle.split()

    mjd = float(s[0])
    date = ephem.Date(2400000 + mjd - 2415020.0).__str__()

    tle = {
        'mjd': mjd,
        'time': ephem.Date(2400000 + mjd - 2415020.0).datetime(),
        'tle': ' '.join(s[1:]),
        'period': 1440.0/float(s[1]),
        'inclination': float(s[2]),
        'eccentricity': float(s[3])
    }

    return tle

def update_satellite_variability(id=0):
    satellite = Satellites.objects.get(id=id)

    try:
        satellite.variability = satellite.satellitetracks_set.aggregate(Max('variability'))['variability__max']
        satellite.variability_period = satellite.satellitetracks_set.filter(variability=2).order_by('id').last().variability_period
    except:
        pass

    satellite.save()

@permission_required_or_403('fweb.access_satellites')
def satellite_details(request, id=0, track_id=0, show_records=False):
    context = {}

    if request.method == 'GET':
        if request.GET.get('show_records'):
            show_records = request.GET.get('show_records')

    if request.method == 'POST':
        assert_permission(request, 'fweb.modify_satellites')
        if id:
            # Deleting satellite or its tracks
            action = request.POST.get('action')
            tracks = [int(track_id) for track_id in request.POST.getlist('track_ids[]')]
            comments = request.POST.get('comments')

            if action == 'delete_tracks':
                print "Deleting tracks %s" % tracks
                for track in SatelliteTracks.objects.filter(id__in = tracks):
                    track.delete()

                update_satellite_variability(id)

                return redirect('satellite_details', id=id)

            elif action == 'delete_satellite':
                print "Deleting satellite %s" % id

                satellite = Satellites.objects.get(id=id)

                if satellite:
                    satellite.delete()

                return redirect('satellites_list')

            elif action == 'rename_satellite':
                new_name = request.POST.get('new_name')
                print "Renaming satellite"

                satellite = Satellites.objects.get(id=id)
                satellite.name = new_name
                satellite.save()

                return redirect('satellite_details', id=id)

            elif action == 'merge_satellite':

                new_cat = float(request.POST.get('new_cat'))

                if request.POST.get('new_catid') == '':
                    satellite = Satellites.objects.get(id=id)
                    print "Changing satellite %s catid to %d" % (satellite.id, new_cat)
                    satellite.catalogue = new_cat
                    satellite.save()
                else:
                    new_catid = float(request.POST.get('new_catid'))
                    new_satellite = Satellites.objects.get(catalogue_id=new_catid, catalogue=new_cat)
                    satellite = Satellites.objects.get(id=id)

                    print "Merging satellite %s into %s" % (satellite.id, new_satellite.id)

                    if satellite.id != new_satellite.id:
                        db_query("UPDATE satellite_tracks SET satellite_id=%s WHERE satellite_id=%s", (new_satellite.id, satellite.id))
                        db_query("UPDATE satellite_records SET satellite_id=%s WHERE satellite_id=%s", (new_satellite.id, satellite.id))

                        id = new_satellite.id
                        satellite.delete()
                        return redirect('satellite_details', id=id)

            elif action == 'change_satellite_type':
                new_type = int(request.POST.get('new_type'))
                satellite = Satellites.objects.get(id=id)
                satellite.type = new_type
                satellite.save()

            elif action == 'comments-update':
                satellite = Satellites.objects.get(id=id)
                satellite.comments = comments
                satellite.save()

                return redirect('satellite_details', id=id)

            elif action == 'comments-delete':
                satellite = Satellites.objects.get(id=id)
                satellite.comments = None
                satellite.save()

                return redirect('satellite_details', id=id)

        elif track_id:
            action = request.POST.get('action')
            new_variability = request.POST.get('new_variability')
            new_period = request.POST.get('new_period')
            comments = request.POST.get('comments')

            track = SatelliteTracks.objects.get(id=track_id)
            id = track.satellite_id

            if action == 'update_track':
                track.variability = int(new_variability)
                track.variability_period = float(new_period) if new_period else 0.0
                track.save()

                update_satellite_variability(id)

                return redirect('satellite_track', track_id=track_id)
            if action == 'refine_track_period':
                track.variability_period = refine_track_period(track)
                track.save()

                update_satellite_variability(id)

                return redirect('satellite_track', track_id=track_id)
            elif action == 'delete_track':
                track.delete()
                update_satellite_variability(id)

                return redirect('satellite_details', id=id)

            elif action == 'comments-update':
                satellite = Satellites.objects.get(id=id)
                satellite.comments = comments
                satellite.save()

                return redirect('satellite_track', track_id=track_id)

            elif action == 'comments-delete':
                satellite = Satellites.objects.get(id=id)
                satellite.comments = None
                satellite.save()

                return redirect('satellite_track', track_id=track_id)


    context['show_records'] = show_records

    context['nsatellites'] = Satellites.objects.count()
    context['ntracks'] = SatelliteTracks.objects.count()
    context['time_min'] = SatelliteRecords.objects.aggregate(Min('time'))['time__min']
    context['time_max'] = SatelliteRecords.objects.aggregate(Max('time'))['time__max']

    if track_id:
        track = SatelliteTracks.objects.get(id=track_id)
        satellite = track.satellite
        context['track'] = track
        if show_records:
            records = track.satelliterecords_set.order_by('time')
            context['records'] = records

        context['track_filters'] = [x.filter.name for x in track.satelliterecords_set.distinct('filter')]
        context['track_objects'] = [x.object_id for x in track.satelliterecords_set.distinct('object')]
        context['track_channels'] = [x.channel_id for x in track.satelliterecords_set.distinct('channel_id')]

        context['tle'] = parse_tle(track.tle)
    else:
        satellite = Satellites.objects.get(id=id)
        tracks = satellite.satellitetracks_set.order_by('id')
        tracks = tracks.annotate(time_min=Min('satelliterecords__time'), time_max=Max('satelliterecords__time'))
        #tracks = tracks.extra(select={'stdmag_mean': 'SELECT avg(stdmag) FROM satellite_records WHERE track_id=satellite_tracks.id AND penumbra = 0'})
        context['tracks'] = tracks

    context['norad'] = satellite.id
    context['name'] = satellite.name

    context['satellite'] = satellite

    if satellite.catalogue == 2 or satellite.country == 'CIS':
        assert_permission(request, 'fweb.access_satellites_all')

    return TemplateResponse(request, 'satellites.html', context=context)

@permission_required_or_403('fweb.access_satellites')
def satellite_track_plot(request, track_id=0, width=800, type='stdmag'):
    if type in ['fold', 'ls', 'pdm']:
        return satellite_track_variability_plot(request, track_id, width, type)

    track = SatelliteTracks.objects.get(id=track_id)
    records = track.satelliterecords_set.order_by('time')

    if track.satellite.catalogue == 2:
        assert_permission(request, 'fweb.access_satellites_all')

    fig = Figure(facecolor='white', figsize=(width/72, width*0.5/72), tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()

    #time_start = track.satelliterecords_set.aggregate(Min('record__time')).get('record__time__min')
    time_start = satellite_track_start(track)

    for channel_id in [r.channel_id for r in track.satelliterecords_set.distinct('channel_id')]:
        records = track.satelliterecords_set.filter(channel_id=channel_id).order_by('time')

        xs = []
        ys = []
        x_prev = None
        f_prev = None
        channel_prev = None

        for r in records:
            x = (r.time - time_start).total_seconds()

            if type == 'stdmag':
                y = r.stdmag
            elif type == 'mag':
                y = r.mag
            elif type == 'distance':
                y = r.distance
            elif type == 'phase':
                y = r.phase
            else:
                y = 0

            color = favor2_filters[r.filter_id] if r.filter_id else favor2_filters[0]
            if r.penumbra:
                f = color + [0.3]
            else:
                f = color + [1.0]

            channel = r.channel_id

            if (x_prev and x - x_prev > 0.5) or (f_prev and f != f_prev) or (channel_prev and channel != channel_prev):
                ax.plot(xs, ys, marker='.', color=f_prev, lw=0.1, ms=3)
                xs = []
                ys = []

            xs.append(x)
            ys.append(y)

            x_prev = x
            f_prev = f
            channel_prev = channel

        # Finalize
        if x and y and f_prev:
            ax.plot(xs, ys, marker='.', color=f_prev, lw=0.1, ms=3)

    #ax.scatter(x, y, c=f, marker='.', edgecolors=f)
    #ax.plot(x, y, marker='.')

    #ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d %H:%M:%S'))
    #ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
    ax.xaxis.set_major_formatter(ScalarFormatter(useOffset=False))
    ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))

    ax.set_xlabel("Time since track start, seconds")
    if type == 'stdmag':
        ax.set_ylabel("Standard Magnitude")
        ax.invert_yaxis()
    elif type == 'mag':
        ax.set_ylabel("Apparent Magnitude")
        ax.invert_yaxis()
    elif type == 'distance':
        ax.set_ylabel("Distance, km")
    elif type == 'phase':
        ax.set_ylabel("Phase angle, degrees")

    #fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    time_start_str = time_start.strftime('%Y-%m-%d %H:%M:%S') + '.%03d' % np.round(1e-3*time_start.microsecond)

    ax.set_title("%s %d / %s, track at %s" % (satellite_catalogue(track.satellite.catalogue), track.satellite.catalogue_id, track.satellite.name, time_start_str))

    canvas = FigureCanvas(fig)
    if request.GET.get('format', 'png') == 'pdf':
        response = HttpResponse(content_type='application/pdf')
        canvas.print_figure(response, format='pdf')
    elif request.GET.get('format', 'png') == 'eps':
        response = HttpResponse(content_type='application/postscript')
        canvas.print_figure(response, format='eps')
    else:
        response = HttpResponse(content_type='image/png')
        canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_satellites')
def satellite_plot(request, id=0, width=800, type='stdmag'):
    satellite = Satellites.objects.get(id=id)

    if satellite.catalogue == 2:
        assert_permission(request, 'fweb.access_satellites_all')

    x = []
    y = []
    f = []

    if type == 'period':
        tracks = satellite.satellitetracks_set.all()
        for t in tracks:
            if t.variability == 2:
                x.append(t.satelliterecords_set.all()[0].time)
                y.append(t.variability_period)
    else:
        records = satellite.satelliterecords_set.all()
        for r in records:
            if type == 'phase_stdmag':
                if r.penumbra:
                    continue
                x.append(r.phase)
            else:
                x.append(r.time)

            if type == 'stdmag' or type == 'phase_stdmag':
                y.append(r.stdmag)
            elif type == 'mag':
                y.append(r.mag)
            elif type == 'distance':
                y.append(r.distance)
            elif type == 'phase':
                y.append(r.phase)
            else:
                y.append(0)

            color = favor2_filters[r.filter_id] if r.filter_id else favor2_filters[0]
            if r.penumbra:
                f.append(color + [0.3])
            else:
                f.append(color + [1.0])

    fig = Figure(facecolor='white', figsize=(width/72, width*0.5/72), tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()

    if type == 'period':
        ax.scatter(x, y, c='r', marker='o')
    else:
        ax.scatter(x, y, c=f, marker='.', edgecolors=f, s=1)

    if type == 'phase_stdmag':
        ax.xaxis.set_major_formatter(ScalarFormatter(useOffset=False))
        ax.set_xlabel("Phase angle, degrees")
    else:
        ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d'))
        ax.set_xlabel("Time, UT")

    ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))

    if type == 'stdmag' or type == 'phase_stdmag':
        ax.set_ylabel("Standard Magnitude")
        ax.invert_yaxis()
    elif type == 'mag':
        ax.set_ylabel("Apparent Magnitude")
        ax.invert_yaxis()
    elif type == 'distance':
        ax.set_ylabel("Distance, km")
    elif type == 'phase':
        ax.set_ylabel("Phase angle, degrees")
    elif type == 'period':
        ax.set_ylabel("Light curve period, s")

    fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    ax.set_title("%s %d / %s" % (satellite_catalogue(satellite.catalogue), satellite.catalogue_id, satellite.name))

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

from lomb import PDM
def refine_period(x, y, P0, detrend=True):
    if detrend:
        x = np.array(x)
        y = np.array(y)

        f = np.polyfit(x, y, 2)
        y0 = np.polyval(f, x)

        y = y - y0

    pp = np.arange(P0*0.9, P0*1.1, P0*0.001)
    pdm = PDM(x, y, 1.0/pp)
    pmin = pp[pdm == np.min(pdm)][0]

    pp = np.arange(pmin*0.99, pmin*1.01, pmin*0.0001)
    pdm = PDM(x, y, 1.0/pp)
    pmin = pp[pdm == np.min(pdm)][0]

    return pmin

def refine_track_period(track):
    if track.variability != 2:
        return track.variability_period

    records = track.satelliterecords_set.order_by('time').filter(penumbra=0)
    t0 = records.first().time

    x = np.array([(r.time - t0).total_seconds() for r in records])
    y = np.array([r.stdmag for r in records])

    P0 = track.variability_period

    if len(x) < 2 or np.max(x) < 2.0*track.variability_period:
        return P0
    else:
        P = refine_period(x, y, P0)

    return P

@permission_required_or_403('fweb.access_satellites')
def satellite_track_variability_plot(request, track_id=0, width=800, type='fold'):
    track = SatelliteTracks.objects.get(id=track_id)
    records = track.satelliterecords_set.order_by('time')

    if track.satellite.catalogue == 2:
        assert_permission(request, 'fweb.access_satellites_all')

    fig = Figure(facecolor='white', figsize=(width/72, width*0.5/72), tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()

    time_start = satellite_track_start(track)
    time_end = satellite_track_end(track)
    T = (time_end - time_start).total_seconds()
    if track.variability == 2:
        P = track.variability_period
    else:
        P = None

    xs = []
    ys = []
    fs = []

    for r in records:
        x = (r.time - time_start).total_seconds()

        y = r.stdmag

        color = favor2_filters[r.filter_id] if r.filter_id else favor2_filters[0]
        if r.penumbra:
            f = color + [0.3]
        else:
            f = color + [1.0]

        xs.append(x)
        ys.append(y)
        fs.append(f)

    x = np.array(xs)
    y = np.array(ys)

    fit = np.polyfit(x, y, 2)
    y0 = np.polyval(fit, x)

    # ax.plot((0,0), (min(ys), max(ys)), c='lightgray')
    # ax.plot((track.variability_period,track.variability_period), (min(ys), max(ys)), c='lightgray')

    #ax.scatter(xs, ys, c=fs, marker='.', edgecolors=f)

    if type == 'fold' and P:
        #P = refine_period(x, y-y0, P, detrend=False)

        if int(request.GET.get('refine', 0)):
            P = refine_track_period(track)

        px = np.mod(x, P)

        ax.scatter(px, y-y0, c=fs, marker='.', edgecolors=fs)
        ax.scatter(px - P, y-y0, c=fs, marker='.', edgecolors=fs)
        ax.scatter(px + P, y-y0, c=fs, marker='.', edgecolors=fs)

        ax.axvline(0, c='gray', alpha=0.5)
        ax.axvline(P, c='gray', alpha=0.5)

        if P != track.variability_period:
            ax.set_xlabel("Time, seconds, folded to period %g (original %g) s" % (P, track.variability_period))
        else:
            ax.set_xlabel("Time, seconds, folded to period %g s" % (P))

        ax.set_ylabel("Standard Magnitude")
        ax.invert_yaxis()
    elif type == 'ls':
        freq = np.arange(1.0/T, 5.0, 1.0/T)
        ls = signal.lombscargle(x, y, 2*np.pi*freq)

        #ax.semilogx(freq, ls)
        ax.loglog(freq, ls, drawstyle='steps')

        if P:
            ax.axvline(1.0/P, c='gray', alpha=0.5)
        ax.set_xlabel("Frequency, Hz")
        ax.set_ylabel("Lomb-Scargle periodogram")
    elif type == 'pdm':
        pp = np.arange(P*0.9, P*1.1, P*0.001)
        pdm = PDM(x, y-y0, 1.0/pp)

        ax.plot(pp, pdm, drawstyle='steps')
        ax.set_xlabel("Period, seconds")
        ax.set_ylabel("Phase Dispersion Minimization")

    ax.xaxis.set_major_formatter(ScalarFormatter(useOffset=False))
    ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))

    #fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.0, 0.1)

    time_start_str = time_start.strftime('%Y-%m-%d %H:%M:%S') + '.%03d' % np.round(1e-3*time_start.microsecond)

    ax.set_title("%s %d / %s, track at %s" % (satellite_catalogue(track.satellite.catalogue), track.satellite.catalogue_id, track.satellite.name, time_start_str))

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_satellites')
def satellite_download(request, id=0, track_id=0, param=None, show_records=False):
    response = HttpResponse(request, content_type='text/plain')

    if track_id:
        try:
            track = SatelliteTracks.objects.get(id=track_id)
        except:
            raise Http404('No such track')

        satellite = track.satellite
        #records = SatelliteRecordsSimple.objects.filter(track_id=track_id).order_by('time')
        records = track.satelliterecords_set.order_by('time')

        response['Content-Disposition'] = 'attachment; filename=track_%s.txt' % track_id

        print >>response, "# satellite: %d %s / %s" % (satellite.catalogue_id, satellite.name, satellite.country)
        print >>response, "# catalogue:", satellite.catalogue, satellite_catalogue(satellite.catalogue)
        print >>response, "# track:", track.id
        print >>response, "# variability:", track.variability, {0:'Not variable', 1:'Aperiodic', 2:'Periodic'}.get(track.variability)
        print >>response, "# variability_period:", track.variability_period
        print >>response, "# npoints:", len(records)

        print >>response, "#"

        print >>response, "# Date Time StdMag Mag Filter Penumbra Distance Phase Channel"

        for record in records:
            print >>response, record.time, record.stdmag, record.mag, filter_name(record.filter_id), record.penumbra, record.distance, record.phase, record.channel_id

    elif id and param is not None:
        try:
            satellite = Satellites.objects.get(id=id)
        except:
            raise Http404('No such satellite')

        tracks = satellite.satellitetracks_set.order_by('id')

        response['Content-Disposition'] = 'attachment; filename=satellite_%s_%s.txt' % (param, id)

        print >>response, "# satellite: %d %s / %s" % (satellite.catalogue_id, satellite.name, satellite.country)
        print >>response, "# catalogue:", satellite.catalogue, satellite_catalogue(satellite.catalogue)
        print >>response, "# variability:", satellite.variability, {0:'Not variable', 1:'Aperiodic', 2:'Periodic'}.get(satellite.variability)
        print >>response, "# variability_period:", satellite.variability_period
        print >>response, "# ntracks:", len(tracks)

        print >>response, "#"

        print >>response, "# Date Time Track MJD %s" % param.capitalize()

        for track in tracks:
            if param == 'period':
                if track.variability == 2:
                    time = track.satelliterecords_set.order_by('time').first().time
                    mjd = astropy.time.Time(time).mjd
                    print >>response, time, track.id, mjd, track.variability_period

    elif id == 'all':
        # records = SatelliteRecords.objects.order_by('track').order_by('time')

        # if not has_permission(request, 'fweb.access_satellites_all'):
        #     records = records.exclude(satellite__catalogue=2)
        #     records = records.exclude(satellite__country='CIS',satellite__country__isnull=False)

        # if request.method == 'GET' and request.GET.get('after'):
        #     records = records.filter(track__gt=request.GET.get('after'))

        # if request.method == 'GET' and request.GET.get('before'):
        #     records = records.filter(track__lt=request.GET.get('before'))

        # print >>response, "# Date Time StdMag Mag Filter Penumbra Distance Phase Channel Track Satellite"

        # for record in records:
        #     print >>response, record.time, record.stdmag, record.mag, filter_name(record.filter_id), record.penumbra, record.distance, record.phase, record.channel_id, record.track_id, record.satellite.catalogue_id

        tracks = SatelliteTracks.objects.order_by('id')
        if not has_permission(request, 'fweb.access_satellites_all'):
            tracks = tracks.exclude(satellite__catalogue=2)
            tracks = tracks.exclude(satellite__country='CIS',satellite__country__isnull=False)

        if request.method == 'GET' and request.GET.get('after'):
            tracks = tracks.filter(id__gt=request.GET.get('after'))

        if request.method == 'GET' and request.GET.get('before'):
            tracks = tracks.filter(id__lt=request.GET.get('before'))

        response['Content-Disposition'] = 'attachment; filename=satellites_all.txt'

        print >>response, "# Date Time StdMag Mag Filter Penumbra Distance Phase Channel Track Satellite"

        for i,track in enumerate(tracks):
            #print i,'/',len(tracks)

            #records = SatelliteRecords.get(track__id=track).order_by('track').order_by('time')
            records = track.satelliterecords_set.order_by('time')

            for record in records:
                print >>response, record.time, record.stdmag, record.mag, filter_name(record.filter_id), record.penumbra, record.distance, record.phase, record.channel_id, record.track_id, track.satellite.catalogue_id


    elif id:
        try:
            satellite = Satellites.objects.get(id=id)
        except:
            raise Http404('No such satellite')

        records = satellite.satelliterecords_set.order_by('time')

        if satellite.catalogue == 2 or satellite.country == 'CIS':
            assert_permission(request, 'fweb.access_satellites_all')

        if request.method == 'GET' and request.GET.get('after'):
            records = records.filter(track__gt=request.GET.get('after'))

        if request.method == 'GET' and request.GET.get('before'):
            records = records.filter(track__lt=request.GET.get('before'))

        response['Content-Disposition'] = 'attachment; filename=satellite_%s.txt' % id

        print >>response, "# satellite: %d %s / %s" % (satellite.catalogue_id, satellite.name, satellite.country)
        print >>response, "# catalogue:", satellite.catalogue, satellite_catalogue(satellite.catalogue)
        print >>response, "# variability:", satellite.variability, {0:'Not variable', 1:'Aperiodic', 2:'Periodic'}.get(satellite.variability)
        print >>response, "# variability_period:", satellite.variability_period
        print >>response, "# npoints:", len(records)

        print >>response, "#"

        print >>response, "# Date Time StdMag Mag Filter Penumbra Distance Phase Channel Track"

        for record in records:
            print >>response, record.time, record.stdmag, record.mag, filter_name(record.filter_id), record.penumbra, record.distance, record.phase, record.channel_id, record.track_id

    else:
        satellites = SatellitesView.objects.order_by('catalogue_id')

        if not has_permission(request, 'fweb.access_satellites_all'):
            satellites = satellites.exclude(catalogue=2)
            satellites = satellites.exclude(country='CIS',country__isnull=False)

        response['Content-Disposition'] = 'attachment; filename=satellites_list.txt'

        print >>response, "# nsatellites:", len(satellites)

        print >>response, "#"

        print >>response, "# Catalogue Id Name Country Variability Period StdMag_Clear StdMag_Clear_Sigma StdMag_B StdMag_B_Sigma StdMag_V StdMag_V_Sigma StdMag_R StdMag_R_Sigma StdMag_Clear_Median StdMag_Clear_Min StdMag_Clear_Max"

        for satellite in satellites:
            satellite.country = satellite.country or None
            satellite.variability_period = satellite.variability_period or 0

            satellite.mean_clear = satellite.mean_clear or np.nan
            satellite.mean_b = satellite.mean_b or np.nan
            satellite.mean_v = satellite.mean_v or np.nan
            satellite.mean_r = satellite.mean_r or np.nan

            satellite.sigma_clear = satellite.sigma_clear or np.nan
            satellite.sigma_b = satellite.sigma_b or np.nan
            satellite.sigma_v = satellite.sigma_v or np.nan
            satellite.sigma_r = satellite.sigma_r or np.nan

            satellite.median_clear = satellite.median_clear or np.nan
            satellite.min_clear = satellite.min_clear or np.nan
            satellite.max_clear = satellite.max_clear or np.nan

            print >>response, satellite_catalogue(satellite.catalogue), satellite.catalogue_id, '"%s"' % satellite.name, satellite.country, satellite.variability, satellite.variability_period, "%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f" % (satellite.mean_clear, satellite.sigma_clear, satellite.mean_b, satellite.sigma_b, satellite.mean_v, satellite.sigma_v, satellite.mean_r, satellite.sigma_r, satellite.median_clear, satellite.min_clear, satellite.max_clear)


    return response
