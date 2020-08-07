from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings

import celery
import favor2_celery

class Command(BaseCommand):
    help = 'Extract all stars for a given nights'

    def handle(self, *args, **options):
        if not args:
            self.extractStars(night='all')
        else:
            for night in args:
                self.extractStars(night=night)

    def extractStars(self, night='all', reprocess=False):
        print 'Extracting stars of night %s' % night

        step1 = celery.group(favor2_celery.extractStars.subtask(kwargs={'night':night, 'reprocess':reprocess}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1))

        step1.apply_async()
