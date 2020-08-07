from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.views.decorators.cache import cache_page
from django.db.models import Count, Sum, Min, Max

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.ticker import ScalarFormatter
from matplotlib.patches import Ellipse

import numpy as np
import pyfits, pywcs, posixpath, glob

import settings

from models import RealtimeObjects, MeteorsView, MeteorsNightsView, MeteorsNightsImagesView, MeteorsNightsProcessed, MeteorsShowers

from meteors_plots import meteors_intersections_plot, meteors_radiants_plot, meteors_map_plot

from utils import permission_required_or_403, assert_permission, has_permission

from templatetags.filters import night_date, night_rate

from meteors import Meteor
from meteors_plots import get_solar_longitude, angle_delta, night_to_time

@permission_required_or_403('fweb.access_meteors')
def meteors_nights(request):
    nights = MeteorsNightsView.objects.order_by('-night')
    aggregates = MeteorsView.objects.filter(night__gt='2014_01_01').aggregate(Count('id'), Min('night'), Max('night'))
    aggregates_favor = MeteorsView.objects.filter(night__lt='2014_01_01').aggregate(Count('id'), Min('night'), Max('night'))

    if not has_permission(request, 'fweb.access_realtime'):
        nights = nights.filter(night__in = MeteorsNightsProcessed.objects.all())

    context = {'nights':nights,
               'aggregates':aggregates,
               'aggregates_favor':aggregates_favor}

    return TemplateResponse(request, 'meteors_nights.html', context=context)

@permission_required_or_403('fweb.access_meteors')
@cache_page(600)
def meteors_intersect(request, night='all', size=800):
    response = HttpResponse(content_type='image/png')
    meteors_intersections_plot(response, night=night, size=size)

    return response

#@permission_required_or_403('fweb.access_meteors')
@cache_page(10)
def meteors_radiants(request, night='all', size=800, normalize=False):
    niter = int(request.GET.get('niter', 100))

    response = HttpResponse(content_type='image/png')
    meteors_radiants_plot(response, night=night, size=size, normalize=normalize, niter=niter, vmin=-2, vmax=10)

    return response

#@permission_required_or_403('fweb.access_meteors')
@cache_page(10)
def meteors_map(request, night='all', ra0=0, dec0=0, size=800, normalize=False):
    response = HttpResponse(content_type='image/png')

    meteors_map_plot(response, ra0=float(ra0), dec0=float(dec0), night=night, size=size, msize=float(request.GET.get('msize', 2.0)))

    return response

@permission_required_or_403('fweb.access_meteors')
#@cache_page(600)
def meteors_plot(request, type='ang_vel', night='all', size=800):
    meteors = MeteorsView.objects.all()

    if night and night != 'all':
        meteors = meteors.filter(night=night)

    # if not night or night == 'all':
    #     meteors = meteors.filter(night__gt='2014_01_01')

    if type == 'ang_vel':
        values = meteors.filter(ang_vel__gt=0).values_list('ang_vel')
        values = np.array(values)

    elif type == 'z':
        values = meteors.values_list('z')
        values = np.array(values)

    elif type == 'duration':
        values = meteors.filter(nframes__gt=0).values_list('nframes')
        values = np.array(values)*0.1

    elif type == 'mag_min':
        values = meteors.filter(mag_calibrated=1)
        values_mmt = np.array(values.filter(night__gt='2014_01_01').values_list('mag_min'))
        values_favor = np.array(values.filter(night__lt='2014_01_01').values_list('mag_min'))

    fig = Figure(facecolor='white', figsize=(size/72, size*0.6/72), tight_layout=True)
    ax = fig.add_subplot(111)

    if type == 'ang_vel':
        ax.hist(values, range=(0,40), bins=50)

        ax.set_xlabel("Angular Velocity, degrees/s")
        ax.set_ylabel("Number of meteors")

    elif type == 'z':
        ax.hist(values, bins=50)

        ax.set_xlabel("Zenith distance, degrees")
        ax.set_ylabel("Number of meteors")

    elif type == 'duration':
        ax.hist(values, bins=np.arange(0, 5, 0.1))

        ax.set_xlabel("Duration, s")
        ax.set_ylabel("Number of meteors")

    elif type == 'mag_min':
        if len(values_mmt) and len(values_favor):
            ax.hist(values_mmt, range=(-2, 12), bins=40, normed=True, alpha=0.5, color='blue', label='MMT')
            ax.hist(values_favor, range=(-2, 12), bins=40, normed=True, alpha=0.5, color='green', label='FAVOR')

            ax.legend(frameon=False)
            ax.set_ylabel("Fraction of meteors")
        else:
            if len(values_mmt):
                ax.hist(values_mmt, range=(-2, 12), bins=40, normed=False, alpha=0.5, color='blue', label='MMT')
            if len(values_favor):
                ax.hist(values_favor, range=(-2, 12), bins=40, normed=False, alpha=0.5, color='blue', label='FAVOR')
            ax.set_ylabel("Number of meteors")

        ax.set_xlabel("Min Magnitude, calibrated to V")

    elif type == 'rate':
        nights = MeteorsNightsImagesView.objects.order_by('night').filter(night__gt='2014_01_01')
        x, y = [], []

        for n in nights:
            date = night_date(n)
            rate = night_rate(n)

            if n.nmeteors > 10:
                x.append(date)
                y.append(rate)

        ax.plot(x, y, 'o')
        ax.set_xlabel('Date')
        ax.set_ylabel('Rate, meteors/sq.deg/hour')

        ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d'))
        ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))
        ax.margins(0.1, 0.1)

    if night and night != 'all':
        ax.set_title('%d meteors of night %s' % (meteors.count(), night))

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_meteors')
def meteors_list(request, night='all'):
    context = {'night':night}

    meteors = MeteorsView.objects.order_by('-time')

    if night and night != 'all':
        meteors = meteors.filter(night=night)

    context['meteors'] = meteors

    showers = MeteorsShowers.objects.filter(activity='annual').order_by('solar_lon')
    solar_lon = get_solar_longitude(night_to_time(night))

    showers = [s for s in showers if angle_delta(solar_lon, s.solar_lon) < 5]

    context['showers'] = showers
    context['solar_lon'] = solar_lon

    context['other_years_nights'] = MeteorsView.objects.distinct('night').order_by('night').filter(night__endswith=night[5:]).exclude(night=night)

    if night and night != 'all':
        context['night_prev'] = MeteorsView.objects.distinct('night').filter(night__lt=night).order_by('-night').values('night').first()
        context['night_next'] = MeteorsView.objects.distinct('night').filter(night__gt=night).order_by('night').values('night').first()

    return TemplateResponse(request, 'meteors_list.html', context=context)

@permission_required_or_403('fweb.access_meteors')
def meteor_details(request, id=0):
    context = {}

    meteor = MeteorsView.objects.get(id=id)
    object = RealtimeObjects.objects.get(id=id)

    record = object.realtimerecords_set.order_by('time').first()
    if record:
        firstimage = object.realtimeimages_set.filter(time__gte=record.time).order_by('time').first()
        context['firstimage'] = firstimage
        channels = []

        path = posixpath.split(firstimage.filename)[0]

        for s in [(posixpath.split(d)[-1]).split('_') for d in glob.glob(posixpath.join(path, 'channels', '*_*'))]:
            channels.append({'id':s[0], 'filter':s[1]})

        context['channels'] = channels

    context['meteor'] = meteor

    return TemplateResponse(request, 'meteor_details.html', context=context)

@permission_required_or_403('fweb.access_meteors')
#@cache_page(600)
def meteor_plot(request, id=0, type='mask', size=800):
    response = HttpResponse(content_type='image/png')

    if type == 'map':
        meteors_map_plot(response, id=int(id), size=size, msize=float(request.GET.get('msize', 1.0)))

        return response

    meteor = MeteorsView.objects.get(id=id)

    object = RealtimeObjects.objects.get(id=id)
    record = object.realtimerecords_set.order_by('time').first()
    firstimage = object.realtimeimages_set.filter(time__gte=record.time).order_by('time').first()
    path = posixpath.split(firstimage.filename)[0]

    fig = Figure(facecolor='white', figsize=(size/72, size*0.6/72), tight_layout=True)
    ax = fig.add_subplot(111)

    if type == 'mask':
        img = pyfits.getdata(posixpath.join(path, 'analysis', 'mask.fits'), -1)
        aspect = 1.0*img.shape[0]/img.shape[1]
        fig.set_size_inches(size/72, size*aspect/72, forward=True)
        ax.imshow(img, cmap="gray_r", origin='upper')
        m = Meteor(path, load=True)
        ax.autoscale(False)
        ax.plot(m.x_start[m.i_start], m.y_start[m.i_start], 'ro', ms=10)
        ax.plot(m.x_end[m.i_end], m.y_end[m.i_end], 'rx', mew=2, ms=10)

    elif type == 'firstimage':
        object = RealtimeObjects.objects.get(id=id)
        record = object.realtimerecords_set.order_by('time').first()
        if record:
            filename = object.realtimeimages_set.filter(time=record.time).first().filename
        img = pyfits.getdata(filename, -1)
        aspect = 1.0*img.shape[0]/img.shape[1]
        fig.set_size_inches(size/72, size*aspect/72, forward=True)
        ax.imshow(img, cmap="gray_r", origin='upper')

    elif type == 'profile':
        m = Meteor(path, load=True)
        x = np.array(m.lcs[0])
        x = (x - m.a_start[0])*m.pixscale
        if m.a_start[0] > m.a_end[0]:
            x = -x

        for i in xrange(1, len(m.lcs)):
            ax.plot(x, m.lcs[i], label="t=%.2g s" % ((m.time[i-1]-m.time[0]).total_seconds()))
            ax.legend()

            ax.set_xlabel('Position along the trail, degrees')
            ax.set_ylabel('Intensity, rms units')

    elif type == 'a_time':
        m = Meteor(path, load=True)
        t = np.array([(tt - m.time[0]).total_seconds() for tt in m.time])
        a1 = (np.array(m.a_start) - m.a_start[0])*m.pixscale
        a2 = (np.array(m.a_end) - m.a_start[0])*m.pixscale
        if m.a_start[0] > m.a_end[0]:
            a1, a2 = -a1, -a2

        for i in xrange(len(m.a_start)):
            #ax.arrow(t[i], a1[i], 0, a2[i] - a1[i], length_includes_head=True, fc='red', ec='red')
            ax.plot([t[i], t[i]], [a1[i], a2[i]], 'ro-')
            if m.cropped_start[i]:
                ax.plot(t[i], a1[i], 'rx', mew=2, ms=10)
            if m.cropped_end[i]:
                ax.plot(t[i], a2[i], 'rx', mew=2, ms=10)

        ax.plot(t, t*m.ang_vel + a2[0], 'b--', alpha=0.5)

        ax.set_xlabel('Time,seconds')
        ax.set_ylabel('Position along the trail, degrees')
        ax.margins(0.1, 0.1)

    elif type == 'lc_flux':
        m = Meteor(path, load=True)
        t = np.array([(tt - m.time[0]).total_seconds() for tt in m.time])
        flux = np.array(m.flux)
        dflux = np.sqrt(flux)

        #for i in xrange(len(t)):
        #ax.plot([t[i], t[i]+0.1], [flux[i], flux[i]], 'r-')
        #ax.bar(t, flux, 0.1, color='blue', alpha=0.5, edgecolor='black')
        ax.errorbar(t+0.05, flux, xerr=0.05, yerr=dflux, fmt='o')

        ax.set_xlabel('Time, seconds')
        ax.set_ylabel('Flux, counts')
        #ax.margins(0.1, 0.1)

    elif type == 'lc_mag':
        m = Meteor(path, load=True)
        t = np.array([(tt - m.time[0]).total_seconds() for tt in m.time])
        mag = np.array(m.mag)

        #for i in xrange(len(t)):
        #ax.plot([t[i], t[i]+0.1], [flux[i], flux[i]], 'r-')
        #ax.bar(t, flux, 0.1, color='blue', alpha=0.5, edgecolor='black')
        ax.errorbar(t+0.05, mag, xerr=0.05, fmt='o')
        ax.invert_yaxis()

        ax.set_xlabel('Time, seconds')
        ax.set_ylabel('Magnitude / %s' % m.filter)
        #ax.margins(0.1, 0.1)

    elif type == 'lc_mags':
        m = Meteor(path, load=True)
        t = np.array([(tt - m.time[0]).total_seconds() for tt in m.time])
        if m.channels:
            c_B = m.getChannel('B')
            c_V = m.getChannel('V')
            c_R = m.getChannel('R')

            for c in [c_B, c_V, c_R]:
                if c:
                    idx = np.abs(c['mags_err']/c['mags']) > 0.33
                    c['mags'][idx] = None
                    #print c

            if c_B:
                tt = np.array([(tt - m.time[0]).total_seconds() for tt in c_B['times']])
                ax.errorbar(tt+0.05, c_B['mags'], xerr=0.05, yerr=c_B['mags_err'], color='blue', label='B')
            if c_V:
                tt = np.array([(tt - m.time[0]).total_seconds() for tt in c_V['times']])
                ax.errorbar(tt+0.05, c_V['mags'], xerr=0.05, yerr=c_V['mags_err'], color='green', label='V')
            if c_R:
                tt = np.array([(tt - m.time[0]).total_seconds() for tt in c_R['times']])
                ax.errorbar(tt+0.05, c_R['mags'], xerr=0.05, yerr=c_R['mags_err'], color='red', label='R')

        ax.legend()
        ax.invert_yaxis()
        ax.set_xlabel('Time, seconds')
        ax.set_ylabel('Magnitudes')

    elif type == 'lc_colors':
        Teff = np.array([100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000])
        BmV = np.array([ 74.94499781,  37.30294507,  24.74089549,  18.43392893,
                         14.63703432,  12.10495799,  10.30013473,   8.95073077,
                         7.90436206,   7.06918046,   3.29596271,   1.99509939,
                         1.32611231,   0.91920823,   0.64754567,   0.45497637,
                         0.31252121,   0.20364148,   0.11821526,  -0.23593559,
                         -0.33787083,  -0.38490831,  -0.41177812,  -0.41012146,
                         -0.34202301,  -0.39071208,  -0.47940761,  -0.50326866])
        VmR = np.array([  5.94723415e+01,   2.92019783e+01,   1.89579720e+01,
                          1.37900667e+01,   1.06850528e+01,   8.62694817e+00,
                          7.17422389e+00,   6.10228757e+00,   5.28420223e+00,
                          4.64275306e+00,   1.96257237e+00,   1.15631315e+00,
                          7.69468231e-01,   5.44283108e-01,   3.98698675e-01,
                          2.97994490e-01,   2.24870006e-01,   1.69757430e-01,
                          1.26966317e-01,  -4.72539555e-02,  -9.71363142e-02,
                          -1.19919531e-01,  -1.13933925e-01,  -6.01614764e-02,
                          -1.59920980e-01,  -1.79039022e-01,  -1.79109749e-01,
                          -1.79165191e-01])

        m = Meteor(path, load=True)
        t = np.array([(tt - m.time[0]).total_seconds() for tt in m.time])
        if m.channels:
            c_B = m.getChannel('B')
            c_V = m.getChannel('V')
            c_R = m.getChannel('R')

            if c_B and c_V and c_R and len(c_B['times']) == len(c_V['times']) and len(c_B['times']) == len(c_R['times']):
                for c in [c_B, c_V, c_R]:
                    idx = np.abs(c['mags_err']/c['mags']) > 0.33
                    c['mags'][idx] = None
                    #print c

                BV = c_B['mags'] - c_V['mags']
                VR = c_V['mags'] - c_R['mags']

                ax.errorbar(VR, BV, xerr=np.hypot(c_V['mags_err'], c_R['mags_err']), yerr=np.hypot(c_B['mags_err'], c_V['mags_err']))

                ax.quiver(VR[:-1], BV[:-1], VR[1:]-VR[:-1], BV[1:]-BV[:-1], scale_units='xy', angles='xy', scale=1, alpha=0.3)

        #ax.autoscale(False)

        idx = (BmV > -0.3) & (BmV < 2) & (VmR > -0.5) & (VmR < 1.5)

        ax.plot(VmR[idx], BmV[idx], '-o', color='gray')
        for i in xrange(len(VmR[idx])):
            ax.text(VmR[idx][i], BmV[idx][i], Teff[idx][i], ha='left', va='bottom')
        #ax.set_aspect(1)
        ax.invert_yaxis()

        ax.set_xlabel('B-V')
        ax.set_ylabel('V-R')


    canvas = FigureCanvas(fig)
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_meteors')
def meteors_download(request, night='all'):
    context = {'night':night}

    meteors = MeteorsView.objects.order_by('-time')

    if night and night != 'all':
        meteors = meteors.filter(night=night)

    response = HttpResponse(request, content_type='text/plain')

    print >>response, "# id night date time ra_start dec_start ra_end dec_end z nframes ang_vel mag_min mag_max filter"
    for meteor in meteors:
        mag = 99
        mag_max = 99
        if meteor.mag_calibrated:
            mag = meteor.mag_min
            mag_max = meteor.mag_max
        print >>response, meteor.id, meteor.night, meteor.time, meteor.ra_start, meteor.dec_start, meteor.ra_end, meteor.dec_end, meteor.z, meteor.nframes, meteor.ang_vel, mag, mag_max, meteor.filter

    return response

@permission_required_or_403('fweb.access_meteors')
def meteor_download_track(request, id=0):
    meteor = MeteorsView.objects.get(id=id)
    path = posixpath.join(settings.BASE_ARCHIVE, 'meteor', meteor.night, str(meteor.id))
    m = Meteor(path, load=True)

    response = HttpResponse(request, content_type='text/plain')

    print >>response, "# id night date_start time ra_start dec_start ra_end dec_end mag"

    for i in xrange(m.nframes):
        print >>response, meteor.id, meteor.night, m.time[i], m.ra_start[i], m.dec_start[i], m.ra_end[i], m.dec_end[i], m.mag[i]

    return response
