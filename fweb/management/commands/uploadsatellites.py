from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings

import celery
import favor2_celery

class Command(BaseCommand):
    help = 'Uploads satellites from pre-configured directory'

    def handle(self, *args, **options):
        celery.chain(
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Uploading satellites started'}, queue="beholder", immutable=True),
            # Upload
            favor2_celery.uploadSatellites.subtask(queue="beholder", immutable=True),
            # Refresh
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Refreshing database views'}, queue="beholder", immutable=True),
            favor2_celery.refreshViews.subtask(queue="beholder", immutable=True),
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Refreshing views done'}, queue="beholder", immutable=True),
            # Finish
            favor2_celery.logMessage.subtask(kwargs={'id':'news', 'text':'Uploading satellites finished'}, queue="beholder", immutable=True),
        ).apply_async()
