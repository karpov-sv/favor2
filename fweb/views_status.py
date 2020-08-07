from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect

from models import BeholderStatus

from django.views.decorators.cache import cache_page

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.patches import Ellipse
from matplotlib import cm

import numpy as np
import datetime
import re

from webchannel_plots import beholder_plot_map, beholder_plot_status

from utils import permission_required_or_403, assert_permission, db_query

@permission_required_or_403('fweb.access_site')
def status_view(request, hours=24, delay=0):
    context = {'hours':int(hours) if hours else 24,
               'delay':int(delay) if delay else 0}

    context['hours_list'] = (3, 6, 12, 24, 48)

    return TemplateResponse(request, 'status.html', context=context)

@permission_required_or_403('fweb.access_site')
@cache_page(5)
def status_plot(request, param='hw_camera_temperature', title='', width=1000, height=500, hours=24, delay=0, nchannels=9, nchillers=9, mode='channels'):
    hours = int(hours) if hours else 24
    delay = int(delay) if delay else 0

    if mode == 'can':
        query = ', '.join(["(status->'can_chiller%d_%s')::float as value%d" % (i+1, param, i+1) for i in xrange(nchillers)])
        N = nchillers
    else:
        query = ', '.join(["(status->'channel%d_%s')::float as value%d" % (i+1, param, i+1) for i in xrange(nchannels)])
        N = nchannels

    query = "SELECT id, time, " + query + " FROM beholder_status WHERE time > %s AND time < %s ORDER BY time DESC"

    # bs = BeholderStatus.objects.raw(query, (datetime.datetime.utcnow() - datetime.timedelta(hours=hours) - datetime.timedelta(hours=delay), datetime.datetime.utcnow() - datetime.timedelta(hours=delay)))
    bs = db_query(query, (datetime.datetime.utcnow() - datetime.timedelta(hours=hours) - datetime.timedelta(hours=delay), datetime.datetime.utcnow() - datetime.timedelta(hours=delay)))
    
    if not title:
        title = {
            'hw_camera_temperature':'Camera Temperature',
            'hw_camera_humidity':'Camera Humidity',
            'hw_celostate_temperature':'Celostate Temperature',
            'hw_celostate_humidity':'Celostate Humidity',
            'andor_temperature':'Andor Temperature',
            'hw_camera':'Camera Power',
            'hw_cover':'Cover',
            'hw_lamp':'Flatfield Lamp',
            't_cold':'Temperature in Cold Contour',
            't_hot':'Temperature in Hot Contour',
        }.get(param, param)

    values = [[] for i in xrange(N)]
    time = []

    for status in bs:
        for i in xrange(N):
            values[i].append(status[2+i])

        time.append(status[1])

    fig = Figure(facecolor='white', dpi=72, figsize=(width/72, height/72), tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()
    ax.set_color_cycle([cm.spectral(k) for k in np.linspace(0, 1, 9)])

    for i in xrange(N):
        if mode == 'can':
            ax.plot(time, values[i], '-', label="Chiller %d" % (i+1))
        else:
            ax.plot(time, values[i], '-', label="Channel %d" % (i+1))

    if time: # It is failing if no data are plotted
        ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d %H:%M:%S'))

    ax.set_xlabel("Time, UT")
    ax.set_ylabel(title)

    fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles, labels, loc=2)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_site')
@cache_page(5)
def status_custom_plot(request, param='weather_ambient_temp', title='', width=1000, height=500, hours=24, delay=0):
    hours = int(hours) if hours else 24
    delay = int(delay) if delay else 0

    #bs = BeholderStatus.objects.raw("SELECT id, time, status->%s as value FROM beholder_status WHERE time > %s AND time < %s ORDER BY time DESC", (param, datetime.datetime.utcnow() - datetime.timedelta(hours=hours) - datetime.timedelta(hours=delay), datetime.datetime.utcnow() - datetime.timedelta(hours=delay)))
    bs = db_query("SELECT id, time, status->%s as value FROM beholder_status WHERE time > %s AND time < %s ORDER BY time DESC", (param, datetime.datetime.utcnow() - datetime.timedelta(hours=hours) - datetime.timedelta(hours=delay), datetime.datetime.utcnow() - datetime.timedelta(hours=delay)))

    if not title:
        title = {
            'weather_ambient_temp':'Ambient Temperature',
        }.get(param, param)

    values = []
    time = []

    for status in bs:
        values.append(status[2])
        time.append(status[1])

    fig = Figure(facecolor='white', dpi=72, figsize=(width/72, height/72), tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()

    ax.plot(time, values, '-', label=param)

    if time: # It is failing if no data are plotted
        ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d %H:%M:%S'))

    ax.set_xlabel("Time, UT")
    ax.set_ylabel(title)

    fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

def extract_status(status, prefix):
    res = {}

    prefix = prefix + '_'

    for key, value in status.items():
        if key.startswith(prefix):
            res[key[len(prefix):]] = value

    return res

def parse_time(string):
    # FIXME: parse more time formats!
    return datetime.datetime.strptime(string, "%Y.%m.%d %H:%M:%S")

@permission_required_or_403('fweb.access_site')
def status_beholder(request):
    context = {}

    timestamp = None

    if request.method == 'POST':
        time_string = request.POST.get('time')
        timestamp = parse_time(time_string)
    elif request.method == 'GET':
        if request.GET.has_key('time'):
            time_string = request.GET.get('time')
            timestamp = parse_time(time_string)

    if timestamp:
        status = BeholderStatus.objects.filter(time__lte=timestamp).order_by('-time').first()

        if status:
            context['status'] = status.status
            context['time'] = status.time

            channels = []
            mounts = []

            for id in range(0, int(status.status['nchannels'])):
                channel = extract_status(status.status, 'channel%d' % (id + 1))
                channel['connected'] = status.status['channel%d' % (id + 1)]
                channels.append(channel)

            for id in range(0, int(status.status['nmounts'])):
                mount = extract_status(status.status, 'mount%d' % (id + 1))
                mount['hw_connected'] = status.status['mount%d_connected' % (id + 1)]
                mount['connected'] = status.status['mount%d' % (id + 1)]
                mounts.append(mount)

            context['channels'] = channels
            context['mounts'] = mounts

            scheduler = extract_status(status.status, 'scheduler')
            scheduler['connected'] = status.status['scheduler']
            context['scheduler'] = scheduler

            weather = extract_status(status.status, 'weather')
            weather['connected'] = status.status['weather']
            context['weather'] = weather

            dome = extract_status(status.status, 'dome')
            dome['connected'] = status.status['dome']
            context['dome'] = dome

            can = extract_status(status.status, 'can')
            if can:
                chillers = []
                for id in range(0, int(status.status['can_nchillers'])):
                    chiller = {'state':status.status['can_chiller%s_state' % (id + 1)],
                               'name':'C%d' % (id + 1),
                               'namestate':'C%d:%s' % (id + 1, status.status['can_chiller%s_state' % (id + 1)])}
                    chillers.append(chiller)
                can['chillers'] = chillers
                context['can'] = can

        context['timestamp'] = timestamp

    return TemplateResponse(request, 'status_beholder.html', context=context)

@permission_required_or_403('fweb.access_site')
@cache_page(10)
def status_beholder_sky(request):
    timestamp = datetime.datetime.utcnow()
    channels = []
    mounts = []
    interests = []

    if request.method == 'GET':
        if request.GET.get('time'):
            time_string = request.GET.get('time')
            timestamp = datetime.datetime.strptime(time_string, "%Y.%m.%d %H:%M:%S")

    response = HttpResponse(content_type='image/png')
    beholder_plot_status(response, time=timestamp)

    return response
