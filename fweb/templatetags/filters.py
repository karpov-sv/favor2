from django import template
from django.template.defaultfilters import stringfilter
from django.utils.safestring import mark_safe
from django.db.models import Avg, Min, Max, StdDev
from django.core.urlresolvers import reverse

from fweb.models import Satellites, SatellitesView

import datetime, re, ephem
import numpy as np
import markdown

from fweb import settings

register = template.Library()

@register.filter(name='record_flags')
def record_flags(value):
    states = []

    if value:
        state = int(value)

        if state & 0x01:
            states.append('saturated')
        if state & 0x02:
            states.append('truncated')
        if state & 0x04:
            states.append('blended')
        if state & 0x08:
            states.append('elongated')
        if state & 0x10:
            states.append('merged')
        if state & 0x20:
            states.append('noise')
        if state & 0x40:
            states.append('unreliable')
        if state & 0x100:
            states.append('bad')

    return ", ".join(states)

@register.filter
def subtract(value, arg):
    return value - arg

@register.filter
def GET_remove(value, key):
    value = value.copy()

    if key in value:
        value.pop(key)

    return value

@register.filter
def GET_append(value, key, new=1):
    value = value.copy()

    if key in value:
        value.pop(key)

    if '=' in key:
        s = key.split('=')
        value.appendlist(s[0], s[1])
    else:
        value.appendlist(key, new)

    return value

@register.filter
def GET_urlencode(value):
    return value.urlencode()

@register.filter
def fromtimestamp(value):
    return datetime.datetime.fromtimestamp(float(value))

@register.filter
def make_label(text, type="primary"):
    return mark_safe("<span class='label label-" + type + "'>" + text + "</span>");

@register.filter
def channel_state(value):
    value = int(value)

    states = {0:"Normal", 1:"Darks", 2:"Autofocus", 3:"Monitoring", 4:"Follow-up", 5:"Flats", 6:"Locate", 7:"Reset", 8:"Calibrate", 9:"Imaging", 100:"Error"}
    types = {0:"success", 1:"success", 2:"info", 3:"info", 4:"info", 5:"info", 6:"warning", 7:"warning", 8:"warning", 100:"danger"}

    return make_label(states.get(value, "Unknown"), types.get(value, "info"))

@register.filter
def mount_state(value):
    value = int(value)

    states = {"-1":"Error", 0:"Idle", 1:"Slewing", 2:"Tracking", 3:"Tracking", 4:"Manual"}
    types = {"-1":"danger", 0:"success", 1:"info", 2:"warning", 3:"warning", 4:"danger"}

    return make_label(states.get(value, "Unknown"), types.get(value, "info"))

@register.filter
def cloud_state(value):
    value = int(value)

    states = {0:"Clouds Unknown", 1:"Clear", 2:"Cloudy", 3:"Very Cloudy"}
    types = {0:"info", 1:"success", 2:"warning", 3:"danger"}

    return make_label(states.get(value, "Unknown"), types.get(value, "info"))

@register.filter
def wind_state(value):
    value = int(value)

    states = {0:"Wind Unknown", 1:"Calm", 2:"Windy", 3:"Very Windy"}
    types = {0:"info", 1:"success", 2:"warning", 3:"danger"}

    return make_label(states.get(value, "Unknown"), types.get(value, "info"))

@register.filter
def rain_state(value):
    value = int(value)

    states = {0:"Rain Unknown", 1:"Dry", 2:"Wet", 3:"Rain"}
    types = {0:"info", 1:"success", 2:"warning", 3:"danger"}

    return make_label(states.get(value, "Unknown"), types.get(value, "info"))

@register.filter
def day_state(value):
    value = int(value)

    states = {0:"Brightness Unknown", 1:"Dark", 2:"Bright", 3:"Very Bright"}
    types = {0:"info", 1:"success", 2:"warning", 3:"danger"}

    return make_label(states.get(value, "Unknown"), types.get(value, "info"))

@register.filter
def satellite_ntracks(satellite):
    if satellite.__dict__.has_key('ntracks'):
        return satellite.__dict__['ntracks']
    else:
        return satellite.satellitetracks_set.count()

@register.filter
def satellite_nrecords(satellite):
    if satellite.__dict__.has_key('nrecords'):
        return satellite.__dict__['nrecords']
    else:
        return satellite.satelliterecords_set.count()

@register.filter
def satellite_mean_stdmag(satellite, filter=0):
    name = {0:'mean_clear',
            1:'mean_b',
            2:'mean_v',
            3:'mean_r'}.get(filter, 'mean_clear')

    if satellite.__dict__.has_key(name):
        return satellite.__dict__[name]
    else:
        return satellite.satelliterecords_set.filter(filter=filter).filter(penumbra=0).aggregate(Avg('stdmag')).get('stdmag__avg')

@register.filter
def satellite_median_stdmag(satellite, filter=0):
    name = {0:'median_clear',
            1:'median_b',
            2:'median_v',
            3:'median_r'}.get(filter, 'mean_clear')

    if satellite.__dict__.has_key(name):
        return satellite.__dict__[name]
    else:
        sats = SatellitesView.objects.filter(id=satellite.id)
        if sats:
            tsat = sats[0] # SatellitesView.objects.get(id=satellite.id)
            return tsat.__dict__[name]
        else:
            return 0;

@register.filter
def satellite_min_stdmag(satellite, filter=0):
    name = {0:'min_clear',
            1:'min_b',
            2:'min_v',
            3:'min_r'}.get(filter, 'mean_clear')

    if satellite.__dict__.has_key(name):
        return satellite.__dict__[name]
    else:
        return satellite.satelliterecords_set.filter(filter=filter).filter(penumbra=0).aggregate(Min('stdmag')).get('stdmag__min')

@register.filter
def satellite_max_stdmag(satellite, filter=0):
    name = {0:'max_clear',
            1:'max_b',
            2:'max_v',
            3:'max_r'}.get(filter, 'mean_clear')

    if satellite.__dict__.has_key(name):
        return satellite.__dict__[name]
    else:
        return satellite.satelliterecords_set.filter(filter=filter).filter(penumbra=0).aggregate(Max('stdmag')).get('stdmag__max')

@register.filter
def satellite_stddev_stdmag(satellite, filter=0):
    name = {0:'sigma_clear',
            1:'sigma_b',
            2:'sigma_v',
            3:'sigma_r'}.get(filter, 'sigma_clear')

    if satellite.__dict__.has_key(name):
        return satellite.__dict__[name]
    else:
        return satellite.satelliterecords_set.filter(filter=filter).filter(penumbra=0).aggregate(StdDev('stdmag')).get('stdmag__stddev')

@register.filter
def satellite_track_start(track):
    if track.__dict__.has_key('time_min'):
        return track.time_min
    else:
        return track.satelliterecords_set.aggregate(Min('time')).get('time__min')

@register.filter
def satellite_track_end(track):
    if track.__dict__.has_key('time_max'):
        return track.time_max
    else:
        return track.satelliterecords_set.aggregate(Max('time')).get('time__max')

@register.filter
def satellite_track_duration(track):
    tmin = satellite_track_start(track)
    tmax = satellite_track_end(track)

    if tmin and tmax:
        return (tmax - tmin).total_seconds()
    else:
        return 0

@register.filter
def satellite_track_penumbra(track):
    if track.satelliterecords_set.filter(penumbra=1).count():
        return True
    else:
        return False

@register.filter
def satellite_catalogue(cat):
    return {1:'NORAD',
            2:'Russian',
            3:'McCants'}.get(int(cat), 'Unknown')

@register.filter
def satellite_type(cat):
    return {0:'Uncertain Satellite',
            1:'Active Satellite',
            2:'Inactive Satellite',
            3:'Rocket Body',
            4:'Debris',
            5:'Mission Debris',
            6:'Fragmentation Debris',
            7:'Unidentified'}.get(int(cat), 'Unknown')

@register.filter
def satellite_type_short(cat):
    return {0:'U/SAT',
            1:'ACT',
            2:'INACT',
            3:'R/B',
            4:'DEB',
            5:'M/DEB',
            6:'F/DEB',
            7:'UNIDENT'}.get(int(cat), 'UNKNOWN')

def night_url(m):
    night = m.group(1)

    return "<a href='%s'>%s</a>" % (reverse('night_summary', args=(night,)), night)

@register.filter
def urlify_news(string):
    string = re.sub(r'\b(\d\d\d\d_\d\d_\d\d)\b', night_url, string)

    return mark_safe(string)

@register.filter
def night_date(night):
    return datetime.datetime.strptime(night.night, '%Y_%m_%d')

@register.filter
def night_moon_phase(night):
    obs = ephem.Observer()
    obs.lat = 3.14159265359/180.0*settings.LATITUDE
    obs.lon = 3.14159265359/180.0*settings.LONGITUDE
    obs.elevation = settings.ELEVATION

    obs.date = night_date(night)
    obs.date = obs.date + 0.5

    moon = ephem.Moon()
    moon.compute(obs)

    return moon.moon_phase

@register.filter
def night_coverage(night):
    return 100.0*night.nimages*10.0/3600

@register.filter
def night_rate(night):
    coverage = night_coverage(night)
    return night.nmeteors/coverage if coverage > 0 else 0

@register.filter
def linecount(text):
    return 0

@register.filter
def filter_name(value):
    #value = int(value)

    return {0:'Clear',
            1:'B',
            2:'V',
            3:'R',
            4:'Pol',
            5:'B+Pol',
            6:'V+Pol',
            7:'R+Pol'}.get(value, "Unknown")

@register.filter
def to_sexadecimal(value, plus=False):
    avalue = np.abs(value)
    deg = int(np.floor(avalue))
    min = int(np.floor(60.0*(avalue - deg)))
    sec = 3600.0*(avalue - deg - 1.0*min/60)

    string = '%02d %02d %04.1f' % (deg, min, sec)

    if value < 0:
        string = '-' + string
    elif plus:
        string = '+' + string

    return string

@register.filter
def to_sexadecimal_plus(value):
    return to_sexadecimal(value, plus=True)

@register.filter
def to_sexadecimal_hours(value):
    return to_sexadecimal(value*1.0/15)

@register.filter
def split(value, arg):
    return value.split(arg)

@register.filter
def markdownify(text):
    # safe_mode governs how the function handles raw HTML
    return markdown.markdown(text, safe_mode='escape')

@register.filter
def angvel(value):
    return float(value)*16.0

@register.filter
def get(d, key):
    return d.get(key, '')

@register.filter
def seconds_since(t, t0):
    return (t - t0).total_seconds()
