from django.template.response import TemplateResponse
from django.db.models import Avg, Min, Max, StdDev

from models import Log, RealtimeComments, Satellites, SatelliteTracks, SatelliteRecords, MeteorsView, MeteorsNightsProcessed
from utils import permission_required_or_403

@permission_required_or_403('fweb.access_site')
def index(request):
    context = {}

    news = Log.objects.filter(id='news').order_by('-time')[:10]
    context['news'] = news

    comments = RealtimeComments.objects.order_by('-object')[:5]
    context['comments'] = comments

    context['nsatellites'] = Satellites.objects.count()
    context['ntracks'] = SatelliteTracks.objects.count()
    context['satellites_time_min'] = SatelliteRecords.objects.aggregate(Min('time'))['time__min']
    context['satellites_time_max'] = SatelliteRecords.objects.aggregate(Max('time'))['time__max']
    context['nmeteors'] = MeteorsView.objects.count()
    context['meteors_time_min'] = MeteorsView.objects.aggregate(Min('time'))['time__min']
    context['meteors_time_max'] = MeteorsView.objects.aggregate(Max('time'))['time__max']

    context['meteors_nights_unprocessed'] = MeteorsView.objects.distinct('night').order_by('-night').exclude(night__in=MeteorsNightsProcessed.objects.all())

    return TemplateResponse(request, 'index.html', context=context)

@permission_required_or_403('fweb.access_beholder')
def links(request):
    return TemplateResponse(request, 'links.html')

@permission_required_or_403('fweb.access_data')
def search(request):
    return TemplateResponse(request, 'search.html')

@permission_required_or_403('fweb.access_beholder')
def channel(request, id=0):
    if int(id) % 2 == 1:
        show_mount = True
    else:
        show_mount = False

    return TemplateResponse(request, 'channel.html', context={'id':id, 'show_mount':show_mount})

@permission_required_or_403('fweb.access_beholder')
def beholder(request):
    return TemplateResponse(request, 'beholder.html')

@permission_required_or_403('fweb.access_beholder')
def mount(request, id=0):
    return TemplateResponse(request, 'mount.html', context={'id':id})
