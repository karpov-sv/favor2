from django.core.management.base import BaseCommand, CommandError

import datetime

from fweb import settings

from favor2 import Favor2

class Command(BaseCommand):
    help = 'Refresh all views in database'

    def handle(self, *args, **options):
        f = Favor2()

        f.query('REFRESH MATERIALIZED VIEW CONCURRENTLY satellites_view')
