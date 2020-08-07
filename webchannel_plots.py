from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from mpl_toolkits.basemap import Basemap

import numpy as np
import ephem
from math import *

from tycho2 import tycho2
from favor2 import Favor2

import datetime

def deg2rad(x):
    return 3.14159265359*x/180.0

def rad2deg(x):
    return 180.0*x/3.14159265359

def beholder_plot_map(file, channels=[], mounts=[], interests=[], size=600, time=None, status=None, extra=None):
    # SAO location
    obs = ephem.Observer()
    obs.lat = deg2rad(43.65722222)
    obs.lon = deg2rad(41.43235417)
    obs.elevation = 2065
    obs.pressure = 0.0

    if time:
        obs.date = time

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

    # Directions of interest
    for interest in interests:
        px, py = map(float(interest['ra']), float(interest['dec']))
        if abs(px) < 1e10 and abs(py) < 1e10:
            map.scatter(px, py, marker=interest.get('marker', 'o'), s=float(interest.get('size', 2000)), alpha=float(interest.get('alpha', 0.1)), color=interest.get('color', 'gray'))
            ax.text(px, py, interest.get('name', ''), color=interest.get('color', 'gray'), va=interest.get('va', 'center'), ha=interest.get('ha', 'center'))

    # Draw mounts
    mp = None
    for mount in mounts:
        px, py = map(float(mount['ra']), float(mount['dec']))
        if abs(px) < 1e10 and abs(py) < 1e10:
            mp = map.scatter(px, py, marker='o', s=300, alpha=0.5, color='blue')
            ax.text(px, py, "%d" % (int(mount['id'])), color='black', va='center', ha='center')

    # Channels
    cp = None
    for channel in channels:
        px, py = map(float(channel['ra']), float(channel['dec']))
        if abs(px) < 1e10 and abs(py) < 1e10:
            if channel.has_key('ra1'):
                px1, py1 = map(float(channel['ra1']), float(channel['dec1']))
                px2, py2 = map(float(channel['ra2']), float(channel['dec2']))
                px3, py3 = map(float(channel['ra3']), float(channel['dec3']))
                px4, py4 = map(float(channel['ra4']), float(channel['dec4']))

                map.plot([px1, px2, px3, px4, px1], [py1, py2, py3, py4, py1], ':', color='red')

            cp = map.scatter(px, py, marker='s', s=200, alpha=0.5, color='red')

            ax.text(px, py, "%d" % (int(channel['id'])), color='black', va='center', ha='center')

    # Title etc
    ax.set_title("%s UT, Sidereal %s" % (obs.date, (ephem.hours(deg2rad(st)))))

    if status:
        text = 'Night\n' if status['is_night'] == '1' else 'Daytime\n'
        text += 'Zsun: %.2f\n' % (float(status['scheduler_zsun']))
        text += 'Zmoon: %.2f' % (float(status['scheduler_zmoon']))
        fig.text(0, 0, text, va='bottom')

        text = 'Weather Good\n' if status['is_weather_good'] == '1' else 'Weather Bad\n'
        if status.has_key('weather_cloud_cond'):
            text += '%s: %.1f\n' % ({0:"Clouds Unknown", 1:"Clear", 2:"Cloudy", 3:"Very Cloudy"}.get(int(status['weather_cloud_cond']), 0), float(status['weather_sky_ambient_temp']))
            text += 'Temperature: %.1f\n' % float(status['weather_ambient_temp'])
            text += '%s: %.1f m/s\n' % ({0:"Wind Unknown", 1:"Calm", 2:"Windy", 3:"Very Windy"}.get(int(status['weather_wind_cond']), 0), float(status['weather_wind']))
            text += 'Humidity: %.0f%%\n' % float(status['weather_humidity'])
            text += '%s\n' % ({0:"Rain Unknown", 1:"Dry", 2:"Wet", 3:"Rain"}.get(int(status['weather_rain_cond']), 0))
        fig.text(1, 0.95, text, ha='right', va='top')

        text = 'State: %s\n' % status['state']
        if status.has_key('dome_state'):
            text += 'Dome: %s\n' % ({0:"Closed", 1:"Opening", 2:"Closing", 3:"Open"}.get(int(status['dome_state']), 'Unknown'))

        if status.has_key('can_nchillers'):
            chillers = False
            for id in xrange(int(status['can_nchillers'])):
                if status['can_chiller'+str(id+1)+'_state'] == '0':
                    chillers = True

            text += 'Chillers: ' + ('On' if chillers else 'Off') + '\n'

        ncameras_on = 0
        nchannels_open = 0
        for id in xrange(int(status['nchannels'])):
            if not status.has_key('channel'+str(id+1)+'_hw_camera'):
                continue
            if status['channel'+str(id+1)+'_hw_camera'] == '1':
                ncameras_on += 1
            if status['channel'+str(id+1)+'_hw_cover'] == '1':
                nchannels_open += 1

        text += 'Cameras: '
        if ncameras_on == 0:
            text += 'Off\n'
        elif ncameras_on == int(status['nchannels']):
            text += 'On\n'
        else:
            text += '%d/%d On\n' % (ncameras_on, int(status['nchannels']))

        text += 'Covers: '
        if nchannels_open == 0:
            text += 'Closed\n'
        elif nchannels_open == int(status['nchannels']):
            text += 'Open\n'
        else:
            text += '%d/%d Open\n'  % (nchannels_open, int(status['nchannels']))

        fig.text(0, 0.95, text, va='top')

        for i in xrange(int(status['nchannels'])):
            if not status.has_key('channel'+str(i+1)+'_state'):
                continue
            x, y = i%3, np.floor(i/3)
            state = {0:"Normal", 1:"Darks", 2:"Autofocus", 3:"Monitoring", 4:"Follow-up", 5:"Flats", 6:"Locate", 7:"Reset", 8:"Calibrate", 9:"Imaging", 100:"Error"}.get(int(status['channel%d_state' % (i+1)]), 100)
            text = state
            fig.text(0.84 + 0.08*x, 0.06-0.03*y, text, ha='right', backgroundcolor='white')

    # if cp or mp:
    #     fig.legend([cp, mp], ["Channels", "Mounts"], loc=4).draw_frame(False)

    if extra:
        for e in extra:
            if e['type'] == 'circle' and e.has_key('ra') and e.has_key('dec'):
                map.tissot(float(e['ra']), float(e['dec']), float(e.get('sr', 1.0)), 50, fc='none', color=e.get('color', 'red'), lw=float(e.get('lw', 1.0)))
                if e.has_key('name'):
                    px, py = map(float(e['ra']), float(e['dec']))
                    if abs(px) < 1e10 and abs(py) < 1e10:
                        ax.text(px, py, e.get('name', ''), color=e.get('color', 'gray'), va=e.get('va', 'center'), ha=e.get('ha', 'center'))

    # Return the image
    canvas = FigureCanvas(fig)
    canvas.print_png(file, bbox_inches='tight')

def beholder_plot_status(file, time=None, favor2=None, status=None, extra=None):
    time = time or datetime.datetime.utcnow()

    if status is None:
        favor2 = favor2 or Favor2()

        res = favor2.query("SELECT * FROM beholder_status WHERE time <= %s ORDER BY time DESC LIMIT 1", (time, ), simplify=True)

        status = res['status']
        time = res['time']

    channels = []
    mounts = []
    interests = []

    for id in xrange(1, int(status['nchannels'])+1):
        if status['channel%d' % (id)] == '1':
            channels.append({'id':id,
                           'ra':float(status['channel%d_ra' % (id)]),
                           'dec':float(status['channel%d_dec' % (id)]),
                           'ra1':float(status['channel%d_ra1' % (id)]),
                           'dec1':float(status['channel%d_dec1' % (id)]),
                           'ra2':float(status['channel%d_ra2' % (id)]),
                           'dec2':float(status['channel%d_dec2' % (id)]),
                           'ra3':float(status['channel%d_ra3' % (id)]),
                           'dec3':float(status['channel%d_dec3' % (id)]),
                           'ra4':float(status['channel%d_ra4' % (id)]),
                           'dec4':float(status['channel%d_dec4' % (id)]),
                           'filter':status['channel%d_hw_filter' % (id)]})

    for id in xrange(1, int(status['nmounts'])+1):
        if status['mount%d' % (id)] == '1':
            mounts.append({'id':id,
                           'ra':float(status['mount%d_ra' % (id)]),
                           'dec':float(status['mount%d_dec' % (id)])})

    if status.has_key('scheduler_ninterests'):
        for id in xrange(int(status['scheduler_ninterests'])):
            interests.append({'name':status['scheduler_interest%d_name' % (id)],
                              'ra':float(status['scheduler_interest%d_ra' % (id)]),
                              'dec':float(status['scheduler_interest%d_dec' % (id)]),
                              'radius':float(status['scheduler_interest%d_radius' % (id)]),
                              'color':'green', 'marker':'o', 'size':2000})

    if status.has_key('scheduler_suggestion_type') and status.has_key('scheduler_suggestion_name'):
        interests.append({'name':status['scheduler_suggestion_name'],
                          'ra':float(status['scheduler_suggestion_ra']),
                          'dec':float(status['scheduler_suggestion_dec']),
                          'color':'magenta', 'marker':'+', 'size':100, 'alpha':0.6, 'va': 'top'})

    return beholder_plot_map(file, channels=channels, mounts=mounts, interests=interests, extra=extra, time=time, status=status)
