from django.template.response import TemplateResponse

from models import Log

from utils import permission_required_or_403, assert_permission

@permission_required_or_403('fweb.access_data')
def logs_list(request, id='all'):
    if not id or id == 'all':
        logs = Log.objects.order_by('-time')
        id = 'all'
    else:
        logs = Log.objects.order_by('-time').filter(id=id)

    ids = [l['id'] for l in Log.objects.values('id').distinct()]
    ids.append('all')

    return TemplateResponse(request, 'logs.html', context={'logs':logs, 'id':id, 'ids':ids})
