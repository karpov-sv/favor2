from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings

import celery
import favor2_celery

class Command(BaseCommand):
    help = 'Find transients for a given nights'

    def handle(self, *args, **options):
        if not args:
            self.findTransients(night='all')
        else:
            for night in args:
                self.findTransients(night=night)

    def findTransients(self, night='all', reprocess=False):
        print 'Finding transients of night %s' % night

        celery.chain(
            celery.group(favor2_celery.findTransients.subtask(kwargs={'night':night, 'reprocess':reprocess}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1)),
            favor2_celery.markReliableTransients.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
        ).apply_async()
