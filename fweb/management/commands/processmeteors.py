from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings

import celery
import favor2_celery

class Command(BaseCommand):
    help = 'Processes all meteors for a given night'

    def handle(self, *args, **options):
        if not args:
            self.processNight(self.getNight(datetime.datetime.now()))
        else:
            for night in args:
                self.processNight(night)

    def getNight(self, t):
        et = t - datetime.timedelta(hours=12)
        return "%04d_%02d_%02d" % (et.year, et.month, et.day)

    def processNight(self, night='all'):
        print 'Processing meteors of night %s' % night

        step1 = favor2_celery.mergeMeteors.subtask(kwargs={'night':night}, queue="beholder", immutable=True)
        step2 = celery.group(favor2_celery.processObjects.subtask(kwargs={'id':0, 'state':'meteor', 'night':night, 'reprocess':False}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1))
        step3 = favor2_celery.mergeMeteors.subtask(kwargs={'night':night}, queue="beholder", immutable=True)

        celery.chain(step1, step2, step3).apply_async()
