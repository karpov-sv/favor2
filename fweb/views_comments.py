from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect

from models import RealtimeComments, RealtimeObjectState

from utils import permission_required_or_403, assert_permission

@permission_required_or_403('fweb.access_data')
def comments_view(request, state='all', preview=False):
    if request.method == 'GET':
        if 'state' in request.GET:
            state = request.GET.get('state')

        if request.GET.get('preview'):
            preview = True

    rt_comments = RealtimeComments.objects.order_by('-object')

    states = ['single', 'meteor', 'moving', 'flash', 'particle', 'misc']
    states.append('all')

    if not state or state == 'all':
        state = 'all'
    else:
        state_id = RealtimeObjectState.objects.get(name=state).id
        rt_comments = rt_comments.filter(object__state = state_id)

    if preview:
        for obj in rt_comments:
            # This step should be optimized somehow
            r = obj.object.realtimerecords_set.order_by('time').first()
            if r:
                i = obj.object.realtimeimages_set.filter(time=r.time).first()
                if i:
                    obj.object.firstimage = i


    context = {}

    context['rt_comments'] = rt_comments
    context['states'] = states
    context['state'] = state

    return TemplateResponse(request, 'comments.html', context = context)
