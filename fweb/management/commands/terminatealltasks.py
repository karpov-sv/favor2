from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings

import celery
import favor2_celery

from celery.task.control import revoke

class Command(BaseCommand):
    help = 'Terminate all running tasks'

    def handle(self, *args, **options):
        inspect = favor2_celery.app.control.inspect()
        a = inspect.active()
        for w in a.values():
            for t in w:
                if t['id']:
                    print "Revoking and terminating task %s at %s" % (t['id'], t['hostname'])
                    revoke(t['id'], terminate=True, signal='SIGKILL')
