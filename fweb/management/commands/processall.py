from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings
from fweb.models import RealtimeObjects

import celery
import favor2_celery

class Command(BaseCommand):
    help = 'Cleanup and postprocess all realtime events for a given night'

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
        if not RealtimeObjects.objects.filter(night=night).exists():
            print 'No data for night %s' % night
            return

        print 'Processing realtime events of night %s' % night

        celery.chain(
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Post-processing of night %s started' % night}, queue="beholder", immutable=True),
            # Satellites
            # favor2_celery.filterSatellites.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
            # favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Satellites for night %s filtered' % night}, queue="beholder", immutable=True),
            # Meteors
            favor2_celery.mergeMeteors.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
            celery.group(favor2_celery.processObjects.subtask(kwargs={'id':0, 'state':'meteor', 'night':night, 'reprocess':False}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1)),
            favor2_celery.mergeMeteors.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
            favor2_celery.processMulticolorMeteors.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Meteors for night %s processed' % night}, queue="beholder", immutable=True),
            # Survey Transients
            #celery.group(favor2_celery.findTransients.subtask(kwargs={'night':night, 'reprocess':False}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1)),
            #favor2_celery.markReliableTransients.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
            #favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Transients for night %s extracted' % night}, queue="beholder", immutable=True),
            # Flashes
            celery.group(favor2_celery.processObjects.subtask(kwargs={'id':0, 'state':'flash', 'night':night, 'reprocess':False}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1)),
            favor2_celery.filterFlashes.subtask(kwargs={'night':night}, queue="beholder", immutable=True),
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Flashes for night %s filtered' % night}, queue="beholder", immutable=True),
            # Survey Photometry
            #celery.group(favor2_celery.extractStars.subtask(kwargs={'night':night, 'reprocess':False}, queue="channel%d" % id, immutable=True) for id in xrange(1, settings.NCHANNELS + 1)),
            #favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Survey photometry for night %s extracted' % night}, queue="beholder", immutable=True),
            # Finish
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Post-processing of night %s finished' % night}, queue="beholder", immutable=True),
        ).apply_async()
