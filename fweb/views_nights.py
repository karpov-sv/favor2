from django.template.response import TemplateResponse
from django.db.models import Count
from django.http import HttpResponse
from django.views.decorators.cache import cache_page

from models import RealtimeObjects, RealtimeObjectState, MeteorsNightsView, MeteorsNightsProcessed
from models import Nights, NightsLatest, Images, SurveyTransients, MeteorsShowers, Alerts

from utils import permission_required_or_403,db_query

from settings import DATABASES

import sys,datetime

from meteors_plots import get_solar_longitude, angle_delta, night_to_time

from dump import dump_meteors, dump_satellites
from favor2 import Favor2

@permission_required_or_403('fweb.access_realtime')
def nights_list(request, all=False):
    # state_meteor = RealtimeObjectState.objects.get(name='meteor').id
    # state_moving = RealtimeObjectState.objects.get(name='moving').id
    # state_flash = RealtimeObjectState.objects.get(name='flash').id

    # nights = Nights.objects.raw("""select night,count(night) as nimages,
    # (select count(*) from realtime_objects where night=i.night) as nobjects,
    # (select count(*) from realtime_objects where night=i.night and state=%s) as nobjects_meteor,
    # (select count(*) from realtime_objects where night=i.night and state=%s) as nobjects_moving,
    # (select count(*) from realtime_objects where night=i.night and state=%s) as nobjects_flash
    # from images i group by night order by night desc""", (state_meteor, state_moving, state_flash,))

    context = {}

    if all:
        nights = Nights.objects.order_by('-night')
    else:
        nights = NightsLatest.objects.order_by('-night')

    context['nights'] = nights
    context['all'] = all

    return TemplateResponse(request, 'nights.html', context=context)

#@permission_required_or_403('fweb.download_data')
def nights_moving(request, night='all', min_length=100, age=0):
    db = DATABASES['favor2']

    if request.method == 'GET':
        if request.GET.get('min_length'):
            min_length = int(request.GET.get('min_length'))
        if request.GET.get('max_age'):
            age = int(request.GET.get('max_age'))

    if age > 24*3600:
        age = 1800

    if age > 0:
        print age, min_length

    f = Favor2(dbname=db['NAME'], dbuser=db['USER'], dbpassword=db['PASSWORD'])

    s = dump_satellites(f, night=night, min_length=min_length, max_age=age, crlf=True, use_zip=True)

    response = HttpResponse(s, content_type='application/x-zip-compressed')

    if age > 0:
        # response['Content-Disposition'] = 'attachment; filename=latest.zip'
        response['Content-Disposition'] = 'attachment; filename=latest_%d_%s.zip' % (age, datetime.datetime.utcnow().strftime('%Y%m%d_%H%M%S'))
    else:
        response['Content-Disposition'] = 'attachment; filename=%s.zip' % night

    return response

@permission_required_or_403('fweb.download_data')
def download_moving(request, id=0):
    db = DATABASES['favor2']

    if request.method == 'GET':
        if request.GET.get('min_length'):
            min_length = int(request.GET.get('min_length'))

    f = Favor2(dbname=db['NAME'], dbuser=db['USER'], dbpassword=db['PASSWORD'])

    s = dump_satellites(f, id=int(id), crlf=True, use_zip=True)

    response = HttpResponse(s, content_type='application/x-zip-compressed')
    response['Content-Disposition'] = 'attachment; filename=moving_%s.zip' % id

    return response

@permission_required_or_403('fweb.download_data')
def download_latest(request, id=0):
    db = DATABASES['favor2']

    if request.method == 'GET':
        if request.GET.get('min_length'):
            min_length = int(request.GET.get('min_length'))

    f = Favor2(dbname=db['NAME'], dbuser=db['USER'], dbpassword=db['PASSWORD'])

    s = dump_satellites(f, id=int(id), crlf=True, use_zip=True)

    response = HttpResponse(s, content_type='application/x-zip-compressed')
    response['Content-Disposition'] = 'attachment; filename=moving_%s.zip' % id

    return response

def night_summary(request, night):
    state_meteor = RealtimeObjectState.objects.get(name='meteor').id
    state_moving = RealtimeObjectState.objects.get(name='moving').id
    state_flash = RealtimeObjectState.objects.get(name='flash').id

    context = {}

    context['night'] = night

    context['num_meteors'] = RealtimeObjects.objects.filter(state=state_meteor).filter(night=night).count()
    try:
        context['num_meteors_analyzed'] = MeteorsNightsView.objects.get(night=night).nmeteors
    except:
        pass

    context['num_moving'] = RealtimeObjects.objects.filter(state=state_moving).filter(night=night).count()
    context['num_flashes'] = RealtimeObjects.objects.filter(state=state_flash).filter(night=night).count()
    context['num_alerts'] = Alerts.objects.filter(night=night).count()

    context['num_images_avg'] = Images.objects.filter(type='avg').filter(night=night).count()
    context['num_images_survey'] = Images.objects.filter(type='survey').filter(night=night).count()

    context['num_survey_transients_reliable'] = SurveyTransients.objects.filter(night=night).extra(where=["(params->'reliable') = '1'"]).count()
    context['num_survey_transients_noident'] = SurveyTransients.objects.filter(night=night, mpc__isnull = True, simbad__isnull = True).extra(where=["(params->'reliable') = '1'"]).count()

    context['night_prev'] = RealtimeObjects.objects.distinct('night').filter(night__lt=night).order_by('-night').values('night').first()
    context['night_next'] = RealtimeObjects.objects.distinct('night').filter(night__gt=night).order_by('night').values('night').first()

    nights =  db_query('select distinct night from realtime_objects order by night', [])
    other_nights = [_[0] for _ in nights if _[0][5:] == night[5:] and _[0] != night]
    # RealtimeObjects.objects.distinct('night').values('night').order_by('night')
    # other_nights = [_['night'] for _ in nights if _['night'][5:] == night[5:] and _['night'] != night]
    context['other_years_nights'] = other_nights

    #context['other_years_nights'] = RealtimeObjects.objects.distinct('night').order_by('night').filter(night__endswith=night[5:]).exclude(night=night)

    context['meteors_processed'] = MeteorsNightsProcessed.objects.filter(night=night).count()

    showers = MeteorsShowers.objects.filter(activity='annual').order_by('solar_lon')
    solar_lon = get_solar_longitude(night_to_time(night))

    showers = [s for s in showers if angle_delta(solar_lon, s.solar_lon) < 5]

    context['showers'] = showers
    context['solar_lon'] = solar_lon

    return TemplateResponse(request, 'night.html', context=context)
