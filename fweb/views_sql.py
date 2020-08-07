from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect

from utils import permission_required_or_403, permission_denied

from settings import DATABASES

import favor2_celery

from favor2 import Favor2

@permission_required_or_403('fweb.access_data')
def sql_view(request, readonly=True):
    if not readonly:
        assert_permission(request, 'modify_data')

    context = {}

    if request.method == 'POST':
        message = ""
        query = request.POST.get('query')

        r = None

        if query:
            r = favor2_celery.runSqlQuery.apply_async(kwargs={'query':query, 'readonly':readonly}, queue="beholder")

        if r:
            # Redirect to the task page
            return redirect('tasks_task', id=r.id)
        else:
            # Redirect to the same view, but with no POST args.
            if readonly:
                return redirect('sql')
            else:
                return redirect('sql_unsafe')

    else:
        # Request the database schema
        db = DATABASES['favor2']
        f = Favor2(dbname=db['NAME'], dbuser=db['USER'], dbpassword=db['PASSWORD'])

        tables = []
        functions = []

        res = f.query("SELECT * FROM information_schema.tables WHERE table_schema='public' ORDER BY table_name", simplify=False)

        for table in res:
            columns = f.query("SELECT * FROM information_schema.columns WHERE table_name=%s ORDER BY ordinal_position", (table['table_name'],), simplify=False)

            tables.append({'name':table['table_name'],
                           'table':table,
                           'columns':columns})

        context['tables'] = tables
        context['functions'] = f.query("SELECT * FROM information_schema.routines WHERE routine_schema='public' ORDER BY routine_name", simplify=False)

    return TemplateResponse(request, 'sql.html', context=context)
