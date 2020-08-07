from django.http import HttpResponse
from django.views.generic import DetailView, ListView
from django.template.response import TemplateResponse
from django.core.servers.basehttp import FileWrapper
from django.db.models import Count
from django.shortcuts import get_object_or_404
from django.db import connection

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.patches import Ellipse

from models import *
import settings

from django.utils import timezone

import os, time, datetime, pytz
import Image, ImageDraw

BASE = 'METEO'

def current_image(request, base=BASE):
    filename = base + '/current.jpg'

    img = Image.open(filename)
    draw = ImageDraw.Draw(img)
    draw.text((10, 10), time.ctime(os.path.getmtime(filename)), "white")

    response = HttpResponse(mimetype="image/jpeg")
    #response['Content-Length'] = os.path.getsize(filename)
    response['Cache-Control'] = 'no-store, no-cache, must-revalidate, max-age=0'
    response['Refresh'] = '1; url=/current.jpg'
    img.save(response, "JPEG", quality=95)
    return response

def current(request):
    response = TemplateResponse(request, 'current.html')
    #response['Refresh'] = '1; url=/'
    return response

def image_jpeg(request, id, size=800, base=BASE, raw=False):
    filename = MeteoImages.objects.get(id=id).filename

    img = Image.open(filename)

    if size:
        img.thumbnail((size,size))

    response = HttpResponse(mimetype="image/jpeg")
    img.save(response, "JPEG", quality=95)
    return response

class ImagesListView(ListView):
    queryset = MeteoImages.objects.order_by('-time')
    context_object_name = 'images_list'
    template_name = 'images.html'

    night = ''

    def get_queryset(self):
        if self.kwargs.has_key('night') and self.kwargs['night']:
            self.night = self.kwargs['night']
            result = MeteoImages.objects.order_by('-time').filter(night=self.night)
        else:
            result = MeteoImages.objects.order_by('-time')

        return result

    def get_context_data(self, **kwargs):
        context = super(ImagesListView, self).get_context_data(**kwargs)
        if self.night:
            context['night'] = self.night
        return context

class ImageDetailView(DetailView):
    model = MeteoImages
    context_object_name = 'image'
    template_name = 'image.html'

def current_sqm(request, size=800, interval=24, field='brightness'):
    dpi = 72
    figsize = (size/dpi, 400/dpi)
    title = {
        'brightness': 'Sky Brightness, mag/sq.arcsec',
        'temp': 'Sensor temperature, C'
    }.get(field, '')

    #sqm = MeteoSqm.objects.filter(time__range=[datetime.datetime.utcnow()-datetime.timedelta(hours=interval), datetime.datetime.utcnow()]).order_by('-time')
    sqm = MeteoSqm.objects.filter(time__range=[timezone.now()-datetime.timedelta(hours=interval), timezone.now()]).order_by('-time')
    x = []
    y = []

    tz = pytz.timezone('UTC')

    for s in sqm:
        x.append(s.time)
        y.append(getattr(s, field))

    fig = Figure(facecolor='white', figsize=figsize)
    ax = fig.add_subplot(111)
    ax.plot_date(x, y, '-', drawstyle='steps')
    ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S', tz=pytz.timezone(settings.TIME_ZONE)))

    ax.set_xlabel("Time")

    if title:
        ax.set_ylabel(title)
    else:
        ax.set_ylabel(field)

    ax.set_title("Sky Quality Meter, %s" % (datetime.datetime.now().strftime('%Y.%m.%d %H:%M:%S MSK')))

    fig.autofmt_xdate()

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/jpeg')
    canvas.print_jpeg(response, dpi=120)

    return response

def current_meteo(request, size=800, interval=24, field='wind'):
    dpi = 72
    figsize = (size/dpi, 400/dpi)
    title = {
        'wind': 'Wind Speed, m/s',
        'ambient_temp': 'Ambient Temperature, C',
        'sky_ambient_temp': 'Sky - Ambient Temperature, C',
        'humidity': 'Humidity, %',
        'dewpoint': 'Dew Point, C'
    }.get(field, '')

    #sqm = MeteoSqm.objects.filter(time__range=[datetime.datetime.utcnow()-datetime.timedelta(hours=interval), datetime.datetime.utcnow()]).order_by('-time')
    sqm = MeteoParameters.objects.filter(time__range=[timezone.now()-datetime.timedelta(hours=interval), timezone.now()]).order_by('-time')
    if field == 'sky_ambient_temp':
        sqm = sqm.filter(sky_ambient_temp__gt = -100)

    x = []
    y = []

    tz = pytz.timezone('UTC')

    for s in sqm:
        x.append(s.time)
        y.append(getattr(s, field))

    fig = Figure(facecolor='white', figsize=figsize)
    ax = fig.add_subplot(111)
    ax.plot_date(x, y, '-', drawstyle='steps')
    ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S', tz=pytz.timezone(settings.TIME_ZONE)))

    ax.autoscale(False)
    
    if field == 'ambient_temp':
        ax.axhline(0, color='red', linestyle='--')
    elif field == 'sky_ambient_temp':
        ax.axhline(-25, color='red', linestyle='--')
        ax.axhline(-40, color='red', linestyle='..')
    elif field == 'wind':
        ax.axhline(10, color='red', linestyle='--')
    elif field == 'humidity':
        ax.axhline(90, color='red', linestyle='--')
    
    ax.set_xlabel("Time")

    if title:
        ax.set_ylabel(title)
    else:
        ax.set_ylabel(field)

    ax.set_title("Boltwood Cloud Sensor II, %s" % (datetime.datetime.now().strftime('%Y.%m.%d %H:%M:%S MSK')))

    fig.autofmt_xdate()

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/jpeg')
    canvas.print_jpeg(response, dpi=120)

    return response

def current_table(request):
    params = MeteoParameters.objects.order_by('-time').first()

    result = "# Date Time sensor_temp sky_ambient_temp ambient_temp wind humidity dewpoint\n"
    result += "%s %g %g %g %g %g %g\n" % (params.time, params.sensor_temp, params.sky_ambient_temp, params.ambient_temp, params.wind, params.humidity, params.dewpoint)
    
    return HttpResponse(result, content_type='text/plain')
