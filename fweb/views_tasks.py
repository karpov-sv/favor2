from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect
from django.core import management

import psycopg2.extras

from utils import assert_is_staff, permission_required_or_403, assert_permission, has_permission

from django.views.decorators.cache import cache_page

from favor2_celery import app

from celery.task.control import revoke

#@cache_page(1)
def tasks_view(request, id=''):
    context = {}

    if request.method == 'POST':
        assert_permission(request, 'fweb.manage_tasks')

        action = request.POST.get('action')

        if action == 'terminatealltasks':
            management.call_command('terminatealltasks')
            return redirect('tasks_list')

        if action == 'processall':
            management.call_command('processall')
            return redirect('tasks_list')

        if action == 'extractstars':
            management.call_command('extractstars')
            return redirect('tasks_list')

        if action == 'findtransients':
            management.call_command('findtransients')
            return redirect('tasks_list')

        if action == 'terminatetask' and id:
            revoke(id, terminate=True, signal='SIGKILL')
            return redirect('tasks_task', id=id)

        if action == 'uploadsatellites':
            management.call_command('uploadsatellites')
            return redirect('tasks_list')

    if id:
        task = app.AsyncResult(id)
        meta = app.backend.get_task_meta(id)
        formatted = None

        if type(task.result) == list or type(task.result) == tuple:
            is_first = True
            formatted = "<table class='table table-striped table-condensed'>"

            for row in task.result:
                formatted = formatted + "<tr>"

                if isinstance(row, psycopg2.extras.DictRow):
                    if is_first:
                        is_first = False

                        for col in row.iteritems():
                            formatted = formatted + "<th>%s</th>" % col[0]

                        formatted = formatted + "</tr><tr>"

                    for col in row.iteritems():
                        formatted = formatted + "<td>%s</td>" % col[1]

                elif type(row) == list or type(row) == tuple:
                    for col in row:
                        formatted = formatted + "<td>%s</td>" % col
                else:
                    formatted = formatted + "<td>%s</td>" % row

                formatted = formatted + "</tr>"

            formatted = formatted + "</table>"

        context = {'task':task, 'meta':meta, 'formatted':formatted}
    else:
        tasks = []

        inspect = app.control.inspect()

        for w in inspect.active().values():
            for t in w:
                t['state'] = 'active'
                if t.has_key('name'):
                    t['shortname'] = t['name'].split('.')[-1]
                tasks.append(t)

        for w in inspect.reserved().values():
            for t in w:
                t['state'] = 'active'
                if t.has_key('name'):
                    t['shortname'] = t['name'].split('.')[-1]
                tasks.append(t)

        for w in inspect.scheduled().values():
            for t0 in w:
                t = t0['request']
                t['state'] = 'scheduled'
                if t.has_key('name'):
                    t['shortname'] = t['name'].split('.')[-1]
                    if t['name'] == 'celery.chord_unlock':
                        t['args'] =  ''
                        t['kwargs'] = ''

                tasks.append(t)

        context = {'tasks':tasks}

    return TemplateResponse(request, 'tasks.html', context=context)
