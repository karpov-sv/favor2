from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect
from django.views.decorators.cache import cache_page

from django.db.models import Count
from django.core.servers.basehttp import FileWrapper

from models import RealtimeObjects, RealtimeRecords, RealtimeObjectState, RealtimeImages, RealtimeComments, RealtimeObjectState, Images, MeteorsNightsProcessed, Alerts

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.patches import Ellipse

import fitsimage
import os, shutil, re, glob
import posixpath

from utils import fix_remote_path, permission_required_or_403, assert_permission
from postprocess import get_filename, get_dirname
from views_raw import get_index
from views_images import find_image_id
import bisect
import datetime

import settings

import favor2_celery
from favor2 import Favor2

def realtime_object_delete(id=0):
    try:
        db = settings.DATABASES['favor2']
        f = Favor2(dbname=db['NAME'], dbuser=db['USER'], dbpassword=db['PASSWORD'])

        f.delete_object(id)
    except:
        pass

def realtime_object_change_state(id=0, state='particle'):
    db = settings.DATABASES['favor2']
    f = Favor2(dbname=db['NAME'], dbuser=db['USER'], dbpassword=db['PASSWORD'])

    f.change_object_state(id, state)

@permission_required_or_403('fweb.access_realtime')
@cache_page(60)
def realtime_nights(request):
    nights = RealtimeObjects.objects.order_by('-night').values('night').annotate(nobjects=Count('night'))

    return TemplateResponse(request, 'realtime_nights.html', context={'nights':nights})

@permission_required_or_403('fweb.access_realtime')
def realtime_objects_list(request, night='', state='all', preview=False, singlepage=False, unanalyzed=False, unclassified=False, analyzed=False, laser=False, min_length=0, channel=0):
    if request.method == 'POST':
        assert_permission(request, 'fweb.modify_data')
        message = ""
        ids = [int(id) for id in request.POST.getlist('object_ids[]')]
        # Make ids unique
        ids = [id for id in set(ids)]
        action = request.POST.get('action')
        r = None

        if action == 'process':
            r = [favor2_celery.processObjects.apply_async(kwargs={'id':ids, 'reprocess':True}, queue="channel%d" % id) for id in xrange(1, settings.NCHANNELS + 1)]
        elif action == 'process_all':
            r = [favor2_celery.processObjects.apply_async(kwargs={'id':0, 'state':state, 'night':night, 'reprocess':False}, queue="channel%d" % id) for id in xrange(1, settings.NCHANNELS + 1)]
        elif action == 'reprocess_all':
            r = [favor2_celery.processObjects.apply_async(kwargs={'id':0, 'state':state, 'night':night, 'reprocess':True}, queue="channel%d" % id) for id in xrange(1, settings.NCHANNELS + 1)]
        elif action == 'preview':
            r = [favor2_celery.processObjects.apply_async(kwargs={'id':ids, 'reprocess':True, 'preview':True}, queue="channel%d" % id) for id in xrange(1, settings.NCHANNELS + 1)]
        elif action == 'preview_all':
            r = [favor2_celery.processObjects.apply_async(kwargs={'id':0, 'state':state, 'night':night, 'reprocess':False, 'preview':True}, queue="channel%d" % id) for id in xrange(1, settings.NCHANNELS + 1)]
        elif action == 'merge_meteors':
            r = favor2_celery.mergeMeteors.apply_async(kwargs={'night':night}, queue="beholder")
        elif action == 'meteors_night_processed' and night and night != 'all':
            n = MeteorsNightsProcessed(night=night)
            n.save()
        elif action == 'filter_laser':
            r = favor2_celery.filterLaserEvents.apply_async(kwargs={'night':night}, queue="beholder")
        elif action == 'filter_satellites':
            r = favor2_celery.filterSatellites.apply_async(kwargs={'night':night}, queue="beholder")
        elif action == 'filter_flashes':
            r = favor2_celery.filterFlashes.apply_async(kwargs={'night':night}, queue="beholder")
        elif action == 'delete':
            for id in ids:
                realtime_object_delete(id=id)

        if r and type(r) != list:
            # Redirect to the task page
            return redirect('tasks_task', id=r.id)
        elif r:
            # Redirect to the task page
            return redirect('tasks_list')
        else:
            # Redirect to the same view, but with no POST args. We can't display messages with it!
            if not state or state == '':
                state = 'all'

            if night and night != 'all':
                return redirect('/realtime/night/%s/%s' % (night, state))
            else:
                return redirect('/realtime/%s' % (state))

    if request.method == 'GET':
        if request.GET.get('preview'):
            preview = True

        if request.GET.get('singlepage'):
            singlepage = True

        if request.GET.get('laser'):
            laser = True

        if request.GET.get('unanalyzed'):
            unanalyzed = True

        if request.GET.get('unclassified'):
            unclassified = True

        if request.GET.get('analyzed'):
            analyzed = True

        if request.GET.get('min_length'):
            min_length = int(request.GET.get('min_length'))

        if request.GET.get('channel'):
            channel = int(request.GET.get('channel'))

    objects = RealtimeObjects.objects.extra(select={'nrecords':'SELECT count(*) FROM realtime_records WHERE object_id=realtime_objects.id', 'nimages':'SELECT count(*) FROM realtime_images WHERE object_id=realtime_objects.id'})

    if request.method == 'GET' and request.GET.get('nframes'):
        nframes = int(request.GET.get('nframes'))
        objects = objects.filter(state=0).extra(where=["(params->'nframes')::int > %s"], params=[nframes])

    if state == 'meteor' and request.method == 'GET' and request.GET.get('ang_vel'):
        ang_vel = float(request.GET.get('ang_vel'))
        objects = objects.filter(state=0).extra(where=["((params->'ang_vel')::float < %s and (params->'ang_vel')::float > 0)"], params=[ang_vel])

    if night and night != 'all':
        objects = objects.filter(night=night)

    if channel:
        objects = objects.filter(channel_id=channel)

    objects = objects.order_by('-time_start')

    if not state or state == 'all':
        state = 'all'
    else:
        state_id = RealtimeObjectState.objects.get(name=state).id
        objects = objects.filter(state = state_id)

    if request.method == 'GET' and request.GET.get('after'):
        objects = objects.filter(night__gt=request.GET.get('after'))
    if request.method == 'GET' and request.GET.get('before'):
        objects = objects.filter(night__lt=request.GET.get('before'))
    if request.method == 'GET' and request.GET.get('duration'):
        objects = objects.extra(where=['extract(epoch from time_end-time_start) > %s'], params=[float(request.GET.get('duration'))])

    if request.method == 'GET' and request.GET.get('sparse'):        
        objects = objects.extra(where=['10.0*extract(epoch from time_end-time_start) > %s*(params->\'length\')::int'], params=[float(request.GET.get('sparse'))])
        
    #states = [s['name'] for s in RealtimeObjectState.objects.values('name')]
    states = ['single', 'meteor', 'moving', 'flash', 'particle', 'misc']
    states.append('all')

    channels = [str(x) for x in range(1, settings.NCHANNELS+1)]

    if min_length:
        objects = objects.extra(where=['(params->\'length\')::int>%s'],params=[min_length])

    if unclassified:
        objects = objects.extra(where=['(params->\'classification\') = \'\''])
        
    if unanalyzed or analyzed or laser: # unclassified or laser:
        objects_filtered = []

        for object in objects:
            if ((unanalyzed and (not object.params.has_key('analyzed') or object.params['analyzed'] != '1')) or
                (unclassified and object.params.has_key('classification') and object.params['classification'] == '') or
                (analyzed and object.params.has_key('analyzed') and object.params['analyzed'] == '1') or
                (laser and (object.params.has_key('laser') and object.params['laser'] == '1'))):
                objects_filtered.append(object)
    else:
        objects_filtered = objects

    # if min_length:
    #     objects = objects_filtered
    #     objects_filtered = []

    #     for object in objects:
    #         if object.nrecords > min_length:
    #             objects_filtered.append(object)

    context = {'objects':objects_filtered, 'night':night, 'state':state, 'states':states, 'channels':channels}

    if state == 'meteor':
        for obj in objects:
            r = obj.realtimerecords_set.order_by('time').first()
            if r:
                obj.filter_id = r.filter_id
    if preview:
        for obj in objects:
            # This step should be optimized somehow
            if state == 'flash':
                r = obj.realtimerecords_set.order_by('mag').first()
            else:
                r = obj.realtimerecords_set.order_by('time').first()
            if r:
                i = obj.realtimeimages_set.filter(time=r.time).first()
                if i:
                    obj.firstimage = i

                if state == 'flash':
                    obj.firstimage0 = obj.realtimeimages_set.order_by('time').first()
                    obj.firstimage1 = obj.realtimeimages_set.order_by('time').last()

    if state == 'flash':
        for obj in objects:
            obj.alert = Alerts.objects.filter(channel_id=obj.channel_id).filter(time=obj.time_start).first()
            if obj.alert:
                if not Images.objects.filter(type='alert').filter(night=obj.night).extra(where=["keywords->'TARGET'='alert_%s'" % obj.alert.id]).exists():
                    obj.alert = None

            rec = RealtimeRecords.objects.filter(object_id=obj.id).order_by('time').first()
            if rec:
                obj.ra0,obj.dec0 = rec.ra,rec.dec

    if night and night != 'all':
        context['night_prev'] = RealtimeObjects.objects.distinct('night').filter(night__lt=night).order_by('-night').values('night').first()
        context['night_next'] = RealtimeObjects.objects.distinct('night').filter(night__gt=night).order_by('night').values('night').first()

    return TemplateResponse(request, 'realtime_objects.html', context=context)

@permission_required_or_403('fweb.access_realtime')
def realtime_object_details(request, object_id=0):
    if request.method == 'POST':
        assert_permission(request, 'fweb.modify_data')
        message = ""
        action = request.POST.get('action')
        r = None

        if action in ['process', 'process_1s', 'process_5s', 'process_10s', 'preview']:
            object = RealtimeObjects.objects.get(id=object_id)

            if action == 'process_1s':
                deltat = 1
            elif action == 'process_5s':
                deltat = 5
            elif action == 'process_10s':
                deltat = 10
            else:
                deltat = 0

            if action == 'preview':
                preview = True
            else:
                preview = False

            r = favor2_celery.processObjects.apply_async(kwargs={'id':object_id, 'deltat':deltat, 'reprocess':True, 'preview':preview}, queue="channel%d" % object.channel_id)

        elif action == 'delete' or action == 'delete_and_next':
            object = RealtimeObjects.objects.get(id=object_id)
            night = object.night
            state = object.state.name
            realtime_object_delete(id=object_id)
            next_state = request.POST.get('next_state')
            if action == 'delete' or not next_state:
                return redirect('/realtime/night/%s/%s' % (night, state))
            else:
                return redirect('realtime_object', object_id=int(next_state))

        elif action == 'comment-delete':
            for c in RealtimeComments.objects.filter(object_id=object_id):
                c.delete()

        elif action == 'comment':
            comment = request.POST.get('comment').strip()
            c,_ = RealtimeComments.objects.get_or_create(object_id=object_id)
            if comment:
                c.comment = comment
                c.save()
            else:
                c.delete()

        elif action == 'change_state':
            new_state = request.POST.get('new_state').strip()
            if new_state:
                realtime_object_change_state(object_id, new_state)
        elif action == 'change_state_particle':
            realtime_object_change_state(object_id, 'particle')
        elif action == 'change_state_misc':
            realtime_object_change_state(object_id, 'misc')

        if r:
            # Redirect to the task page
            return redirect('tasks_task', id=r.id)
        else:
            # Redirect to the same view, but with no POST args. We can't display messages with it!
            return redirect('realtime_object', object_id=object_id)

    object = RealtimeObjects.objects.annotate(nrecords=Count('realtimerecords')).get(id=object_id)

    context = {'object':object}

    next = RealtimeObjects.objects.filter(night=object.night).filter(id__lt=object.id).order_by('-time_start').order_by('-id').first()
    next_state = RealtimeObjects.objects.filter(night=object.night, state=object.state, id__lt=object.id).order_by('-time_start').order_by('-id').first()
    if next:
        context['next'] = next.id
    if next_state:
        context['next_state'] = next_state.id

    prev = RealtimeObjects.objects.filter(night=object.night).filter(id__gt=object.id).order_by('time_start').order_by('id').first()
    prev_state = RealtimeObjects.objects.filter(night=object.night, state=object.state, id__gt=object.id).order_by('time_start').order_by('id').first()
    if prev:
        context['prev'] = prev.id
    if prev_state:
        context['prev_state'] = prev_state.id

    images = RealtimeImages.objects.filter(object_id=object_id).order_by('time')
    if not images:
        records = RealtimeRecords.objects.filter(object_id=object_id).order_by('time')
        images = [{'record':r, 'time':r.time} for r in records]
        context['processed'] = False
    else:
        context['processed'] = True

    comments = RealtimeComments.objects.filter(object_id=object_id)
    if comments:
        context['comment'] = comments[0]

    if object.params.has_key('related'):
        related = object.params['related'].split()
        context['related'] = related

    avg = Images.objects.filter(type='avg').filter(channel_id=object.channel_id).filter(time__gt=object.time_start).order_by('time').first()
    if avg:
        context['avg_id'] = avg.id
    #context['avg_id'] = find_image_id(object.time_start, 'avg', object.channel_id)

    if object.channel_id:
        # Try to get direct links to RAW files
        raw_base = fix_remote_path(settings.BASE_RAW, channel_id = object.channel_id)
        raw_dirs = [d for d in os.listdir(raw_base)
                    if os.path.isdir(posixpath.join(raw_base, d))
                    and re.match('^\d{8}_\d{6}$', d)]
        raw_dirs.sort()

        i0 = max(0, bisect.bisect_right(raw_dirs, get_dirname(object.time_start - datetime.timedelta(seconds=1))) - 1)
        i1 = bisect.bisect_right(raw_dirs, get_dirname(object.time_end + datetime.timedelta(seconds=1))) - 1
        raw_dirs = raw_dirs[i0:i1+1]

        for image in images:
            raw = None

            if raw_dirs:
                time = image['time'] if type(image) == dict else image.time
                filename = get_filename(time)

                for dir in raw_dirs:
                    idx = get_index(posixpath.join(raw_base, dir))

                    for i in idx:
                        if i['path'] == filename:
                            raw = posixpath.join(dir, i['path'])
                            break

                    if raw:
                        break

            if type(image) == dict:
                image['raw'] = raw
            else:
                image.raw = raw

        context['images'] = images

    return TemplateResponse(request, 'realtime_object.html', context=context)

@permission_required_or_403('fweb.access_realtime')
@cache_page(300)
def realtime_object_lightcurve(request, object_id=0, size=1000, type='flux'):
    obj = RealtimeObjects.objects.get(id=object_id)
    records = RealtimeRecords.objects.filter(object_id=object_id).order_by('time')
    x = []
    y = []
    y1 = []
    y2 = []

    for r in records:
        x.append(r.time)
        if type == 'mags':
            y.append(r.mag)
            y1.append(r.mag - r.mag_err)
            y2.append(r.mag + r.mag_err)
        else:
            y.append(r.flux)
            y1.append(r.flux - r.flux_err)
            y2.append(r.flux + r.flux_err)

    fig = Figure(facecolor='white', figsize=(size/72, 600/72), tight_layout=True)
    ax = fig.add_subplot(111)
    ax.autoscale()
    ax.plot_date(x, y, 'o-')
    ax.vlines(x, y1, y2)
    ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))

    ax.set_xlabel("Time")
    if type == 'mags':
        ax.set_ylabel("Magnitude")
        ax.invert_yaxis()
    else:
        ax.set_ylabel("Flux")
    ax.set_title("Object %d at %s" % (obj.id, obj.time_start))

    fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_realtime')
@cache_page(30)
def realtime_object_map(request, object_id=0, size=1000, width=settings.IMAGE_WIDTH, height=settings.IMAGE_HEIGHT):
    obj = RealtimeObjects.objects.get(id=object_id)
    records = RealtimeRecords.objects.filter(object_id=object_id).order_by('time')

    fig = Figure(facecolor='white')
    ax = fig.add_subplot(111, aspect='equal')

    for r in records:
        e = Ellipse(xy=[r.x, r.y], width=r.a, height=r.b, angle=r.theta*180.0/3.1415, edgecolor='r')
        ax.add_artist(e)
        e.set_clip_box(ax.bbox)

    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_title("Object %d at %s" % (obj.id, obj.time_start))

    ax.set_xlim(0, width)
    ax.set_ylim(height, 0)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

#@permission_required_or_403('fweb.access_data')
#@cache_page(30)
def realtime_object_animation(request, object_id=0):
    object = RealtimeObjects.objects.get(id=object_id)
    images = RealtimeImages.objects.filter(object=object_id)[:1]

    if request.method == 'GET':
        channel_id = int(request.GET.get('channel_id', 0))
    else:
        channel_id = 0

    if images:
        dirname = posixpath.split(images[0].filename)[0]
        dirname = posixpath.join(settings.BASE, dirname)
        dirname = fix_remote_path(dirname, object.channel_id)

        if channel_id:
            filename = glob.glob(posixpath.join(dirname, "channels", "%d_*" % channel_id, "animation.gif"))
            if filename:
                filename = filename[0]

            print filename
        else:
            filename = posixpath.join(dirname, "animation.gif")

        # with open(filename) as f:
        #     string = f.read()

        response = HttpResponse(FileWrapper(file(filename)), content_type='image/gif')
        response['Content-Length'] = os.path.getsize(filename)
    else:
        response = HttpResponse("Animation unavailable")

    return response

#@permission_required_or_403('fweb.access_data')
def realtime_image_view(request, image_id=0, size=0, type='jpeg'):
    filename = RealtimeImages.objects.filter(id=image_id)
    if filename:
        object = RealtimeObjects.objects.get(id=filename[0].object_id)
        filename = filename[0].filename
    else:
        return HttpResponse("Image unavailable")

    filename = posixpath.join(settings.BASE, filename)
    filename = fix_remote_path(filename, object.channel_id)

    if type == 'jpeg':
        image,xsize,ysize = fitsimage.FitsReadData(filename)

        img = fitsimage.FitsImageFromData(image, xsize, ysize, contrast="percentile", contrast_opts={'max_percent':99.5}, scale="linear")
        if size:
            img.thumbnail((size,size), resample=fitsimage.Image.ANTIALIAS)
        # now what?
        response = HttpResponse(content_type="image/jpeg")
        img.save(response, "JPEG", quality=95)
    elif type == 'fits':
        response = HttpResponse(FileWrapper(file(filename)), content_type='application/octet-stream')
        response['Content-Disposition'] = 'attachment; filename='+os.path.split(filename)[-1]
        response['Content-Length'] = os.path.getsize(filename)

    return response
