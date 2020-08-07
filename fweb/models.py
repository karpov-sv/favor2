# This is an auto-generated Django model module.
# You'll have to do the following manually to clean this up:
#     * Rearrange models' order
#     * Make sure each model has one field with primary_key=True
# Feel free to rename the models, but don't rename db_table values or field names.
#
# Also note: You'll have to insert the output of 'django-admin.py sqlcustom [appname]'
# into your database.

from django.db import models
from django_hstore.fields import DictionaryField
#from django_hstore.models import HStoreManager

class Filters(models.Model):
    id = models.IntegerField(primary_key=True)
    name = models.TextField(blank=True)
    class Meta:
        db_table = 'filters'
        app_label = 'favor2'

class Stars(models.Model):
    id = models.IntegerField(primary_key=True)
    ra0 = models.FloatField(null=True, blank=True)
    dec0 = models.FloatField(null=True, blank=True)
    class Meta:
        db_table = u'stars'
        app_label = 'favor2'

class Records(models.Model):
    channel_id = models.IntegerField(null=True, blank=True)
    time = models.DateTimeField(null=True, blank=True, primary_key=True)
    filter = models.ForeignKey(Filters, null=True, db_column='filter', blank=True)
    frame = models.ForeignKey('Images', null=True, db_column='frame', blank=True)
    ra = models.FloatField(null=True, blank=True)
    dec = models.FloatField(null=True, blank=True)
    mag = models.FloatField(null=True, blank=True)
    mag_err = models.FloatField(null=True, blank=True)
    cbv = models.FloatField(null=True, blank=True)
    cvr = models.FloatField(null=True, blank=True)
    quality = models.FloatField(null=True, blank=True)
    flux = models.FloatField(null=True, blank=True)
    flux_err = models.FloatField(null=True, blank=True)
    x = models.FloatField(null=True, blank=True)
    y = models.FloatField(null=True, blank=True)
    flags = models.IntegerField(null=True, blank=True)
    class Meta:
        db_table = 'records'
        app_label = 'favor2'

class Tycho2(models.Model):
    name = models.TextField(blank=True)
    ra = models.FloatField(null=True, blank=True)
    dec = models.FloatField(null=True, blank=True)
    bt = models.FloatField(null=True, blank=True)
    vt = models.FloatField(null=True, blank=True)
    class Meta:
        db_table = u'tycho2'
        app_label = 'favor2'

class Images(models.Model):
    id = models.IntegerField(primary_key=True)
    channel_id = models.IntegerField(null=True, blank=True)
    filter = models.ForeignKey(Filters, null=True, db_column='filter', blank=True)
    night = models.TextField(blank=True)
    filename = models.TextField(blank=True)
    time = models.DateTimeField(null=True, blank=True)
    type = models.TextField(blank=True)
    ra0 = models.FloatField(null=True, blank=True)
    dec0 = models.FloatField(null=True, blank=True)
    keywords = DictionaryField()
    class Meta:
        db_table = u'images'
        app_label = 'favor2'

class Log(models.Model):
    time = models.DateTimeField(null=True, blank=True)
    id = models.TextField(blank=True, primary_key=True)
    text = models.TextField(blank=True)
    class Meta:
        db_table = u'log'
        app_label = 'favor2'

class SchedulerTargetStatus(models.Model):
    id = models.IntegerField(primary_key=True)
    name = models.TextField(blank=True)
    class Meta:
        db_table = 'scheduler_target_status'
        app_label = 'favor2'

class SchedulerTargets(models.Model):
    id = models.IntegerField(primary_key=True)
    external_id = models.IntegerField(null=True, blank=True)
    name = models.TextField(blank=True)
    type = models.TextField(blank=True)
    ra = models.FloatField(null=True, blank=True)
    dec = models.FloatField(null=True, blank=True)
    exposure = models.FloatField(null=True, blank=True)
    filter = models.TextField(blank=True)
    repeat = models.IntegerField(blank=True)
    status = models.ForeignKey(SchedulerTargetStatus, null=True, db_column='status', blank=True)
    time_created = models.DateTimeField(null=True, blank=True)
    time_completed = models.DateTimeField(null=True, blank=True)
    uuid = models.TextField(unique=True, blank=True)
    timetolive = models.FloatField(null=True, blank=True)
    params = DictionaryField()
    class Meta:
        db_table = 'scheduler_targets'
        app_label = 'favor2'

class RealtimeObjects(models.Model):
    id = models.IntegerField(primary_key=True)
    channel_id = models.IntegerField(null=True, blank=True)
    night = models.TextField(blank=True)
    time_start = models.DateTimeField(null=True, blank=True)
    time_end = models.DateTimeField(null=True, blank=True)
    state = models.ForeignKey('RealtimeObjectState', db_column='state')
    ra0 = models.FloatField(null=True, blank=True)
    dec0 = models.FloatField(null=True, blank=True)
    params = DictionaryField()
    class Meta:
        db_table = 'realtime_objects'
        app_label = 'favor2'

class RealtimeRecords(models.Model):
    id = models.IntegerField(primary_key=True)
    object = models.ForeignKey(RealtimeObjects, null=True, blank=True)
    time = models.DateTimeField(null=True, blank=True)
    ra = models.FloatField(null=True, blank=True)
    dec = models.FloatField(null=True, blank=True)
    x = models.FloatField(null=True, blank=True)
    y = models.FloatField(null=True, blank=True)
    a = models.FloatField(null=True, blank=True)
    b = models.FloatField(null=True, blank=True)
    theta = models.FloatField(null=True, blank=True)
    flux = models.FloatField(null=True, blank=True)
    flux_err = models.FloatField(null=True, blank=True)
    mag = models.FloatField(null=True, blank=True)
    mag_err = models.FloatField(null=True, blank=True)
    filter = models.ForeignKey(Filters, null=True, db_column='filter', blank=True)
    flags = models.IntegerField(null=True, blank=True)
    params = DictionaryField()
    class Meta:
        db_table = 'realtime_records'
        app_label = 'favor2'

class RealtimeObjectState(models.Model):
    id = models.IntegerField(primary_key=True)
    name = models.TextField(blank=True)
    class Meta:
        db_table = 'realtime_object_state'
        app_label = 'favor2'

class RealtimeImages(models.Model):
    id = models.IntegerField(primary_key=True)
    object = models.ForeignKey('RealtimeObjects')
    record = models.ForeignKey('RealtimeRecords', null=True, blank=True)
    filename = models.TextField()
    time = models.DateTimeField(null=True, blank=True)
    keywords = DictionaryField()
    class Meta:
        db_table = 'realtime_images'
        app_label = 'favor2'

class RealtimeComments(models.Model):
    # id = models.IntegerField(primary_key=True)
    object = models.ForeignKey('RealtimeObjects', unique=True)
    comment = models.TextField()
    class Meta:
        db_table = 'realtime_comments'
        app_label = 'favor2'

class Nights(models.Model):
    night = models.TextField(primary_key=True)
    nimages = models.IntegerField()
    nobjects = models.IntegerField()
    nobjects_meteor = models.IntegerField()
    nobjects_moving = models.IntegerField()
    nobjects_flash = models.IntegerField()
    class Meta:
        db_table = 'nights_view'
        app_label = 'favor2'

class NightsLatest(models.Model):
    night = models.TextField(primary_key=True)
    nimages = models.IntegerField()
    nobjects = models.IntegerField()
    nobjects_meteor = models.IntegerField()
    nobjects_moving = models.IntegerField()
    nobjects_flash = models.IntegerField()
    class Meta:
        db_table = 'nights_latest_view'
        app_label = 'favor2'

class BeholderStatus(models.Model):
    id = models.IntegerField(primary_key=True)
    time = models.DateTimeField(null=True, blank=True)
    status = DictionaryField()
    class Meta:
        db_table = 'beholder_status'
        app_label = 'favor2'

class Satellites(models.Model):
    id = models.IntegerField(primary_key=True)
    catalogue = models.IntegerField(blank=True, null=True)
    catalogue_id = models.IntegerField(blank=True, null=True)
    name = models.TextField(blank=True)
    iname = models.TextField(blank=True)
    country = models.TextField(blank=True)
    type = models.IntegerField(blank=True, null=True)
    launch_date = models.DateTimeField(null=True, blank=True)
    variability = models.IntegerField(blank=True, null=True)
    variability_period = models.FloatField(blank=True, null=True)
    orbit_inclination = models.FloatField(blank=True, null=True)
    orbit_period = models.FloatField(blank=True, null=True)
    orbit_eccentricity = models.FloatField(blank=True, null=True)
    rcs = models.FloatField(blank=True, null=True)
    comments = models.TextField(blank=True)
    params = DictionaryField()
    class Meta:
        db_table = 'satellites'
        app_label = 'favor2'

class SatellitesView(models.Model):
    id = models.IntegerField(primary_key=True)
    catalogue = models.IntegerField(blank=True, null=True)
    catalogue_id = models.IntegerField(blank=True, null=True)
    name = models.TextField(blank=True)
    iname = models.TextField(blank=True)
    country = models.TextField(blank=True, null=True)
    type = models.IntegerField(blank=True, null=True)
    launch_date = models.DateTimeField(blank=True, null=True)
    variability = models.IntegerField(blank=True, null=True)
    variability_period = models.FloatField(blank=True, null=True)
    orbit_inclination = models.FloatField(blank=True, null=True)
    orbit_period = models.FloatField(blank=True, null=True)
    orbit_eccentricity = models.FloatField(blank=True, null=True)
    rcs = models.FloatField(blank=True, null=True)
    comments = models.TextField(blank=True)
    params = DictionaryField()
    ntracks = models.BigIntegerField(blank=True, null=True)
    nrecords = models.BigIntegerField(blank=True, null=True)
    penumbra = models.NullBooleanField()
    mean_clear = models.FloatField(blank=True, null=True)
    mean_b = models.FloatField(blank=True, null=True)
    mean_v = models.FloatField(blank=True, null=True)
    mean_r = models.FloatField(blank=True, null=True)
    sigma_clear = models.FloatField(blank=True, null=True)
    sigma_b = models.FloatField(blank=True, null=True)
    sigma_v = models.FloatField(blank=True, null=True)
    sigma_r = models.FloatField(blank=True, null=True)

    median_clear = models.FloatField(blank=True, null=True)
    median_b = models.FloatField(blank=True, null=True)
    median_v = models.FloatField(blank=True, null=True)
    median_r = models.FloatField(blank=True, null=True)

    min_clear = models.FloatField(blank=True, null=True)
    min_b = models.FloatField(blank=True, null=True)
    min_v = models.FloatField(blank=True, null=True)
    min_r = models.FloatField(blank=True, null=True)

    max_clear = models.FloatField(blank=True, null=True)
    max_b = models.FloatField(blank=True, null=True)
    max_v = models.FloatField(blank=True, null=True)
    max_r = models.FloatField(blank=True, null=True)

    time_last = models.DateTimeField(null=True, blank=True)
    time_first = models.DateTimeField(null=True, blank=True)
    class Meta:
        db_table = 'satellites_view'
        app_label = 'favor2'

class SatelliteTracks(models.Model):
    id = models.IntegerField(primary_key=True)
    satellite = models.ForeignKey(Satellites, blank=True, null=True)
    tle = models.TextField(blank=True)
    age = models.FloatField(blank=True, null=True)
    transversal_shift = models.FloatField(blank=True, null=True)
    transversal_rms = models.FloatField(blank=True, null=True)
    binormal_shift = models.FloatField(blank=True, null=True)
    binormal_rms = models.FloatField(blank=True, null=True)
    variability = models.IntegerField(blank=True, null=True)
    variability_period = models.FloatField(blank=True, null=True)
    class Meta:
        db_table = 'satellite_tracks'
        app_label = 'favor2'

class SatelliteRecords(models.Model):
    id = models.IntegerField(primary_key=True)
    satellite = models.ForeignKey(Satellites, blank=True, null=True)
    track = models.ForeignKey(SatelliteTracks, blank=True, null=True)
    record = models.ForeignKey(RealtimeRecords, blank=True, null=True)
    object = models.ForeignKey(RealtimeObjects, blank=True, null=True)
    time = models.DateTimeField(null=True, blank=True)
    ra = models.FloatField(blank=True, null=True)
    dec = models.FloatField(blank=True, null=True)
    filter = models.ForeignKey(Filters, null=True, db_column='filter', blank=True)
    mag = models.FloatField(blank=True, null=True)
    channel_id = models.IntegerField(blank=True, null=True)
    flags = models.IntegerField(blank=True, null=True)
    stdmag = models.FloatField(blank=True, null=True)
    phase = models.FloatField(blank=True, null=True)
    distance = models.FloatField(blank=True, null=True)
    penumbra = models.IntegerField(null=True, blank=True)
    class Meta:
        db_table = 'satellite_records'
        app_label = 'favor2'

class SatelliteRecordsSimple(models.Model):
    id = models.IntegerField(primary_key=True)
    satellite_id = models.IntegerField(blank=True, null=True)
    track_id = models.IntegerField(blank=True, null=True)
    time = models.DateTimeField(null=True, blank=True)
    ra = models.FloatField(blank=True, null=True)
    dec = models.FloatField(blank=True, null=True)
    filter = models.IntegerField(blank=True, null=True)
    mag = models.FloatField(blank=True, null=True)
    channel_id = models.IntegerField(blank=True, null=True)
    flags = models.IntegerField(blank=True, null=True)
    stdmag = models.FloatField(blank=True, null=True)
    phase = models.FloatField(blank=True, null=True)
    distance = models.FloatField(blank=True, null=True)
    penumbra = models.IntegerField(null=True, blank=True)
    class Meta:
        db_table = 'satellite_records'
        app_label = 'favor2'

class MeteorsView(models.Model):
    id = models.IntegerField(primary_key=True)
    channel_id = models.IntegerField(blank=True, null=True)
    night = models.TextField(blank=True)
    time = models.DateTimeField(blank=True, null=True)
    time_end = models.DateTimeField(blank=True, null=True)
    ra_start = models.FloatField(blank=True, null=True)
    dec_start = models.FloatField(blank=True, null=True)
    ra_end = models.FloatField(blank=True, null=True)
    dec_end = models.FloatField(blank=True, null=True)
    nframes = models.IntegerField(blank=True, null=True)
    ang_vel = models.FloatField(blank=True, null=True)
    z = models.FloatField(blank=True, null=True)
    mag_calibrated = models.IntegerField(blank=True, null=True)
    mag0 = models.FloatField(blank=True, null=True)
    mag_min = models.FloatField(blank=True, null=True)
    mag_max = models.FloatField(blank=True, null=True)
    filter = models.TextField(blank=True, null=True)
    class Meta:
        db_table = 'meteors_view'
        app_label = 'favor2'

class MeteorsNightsView(models.Model):
    night = models.TextField(primary_key=True)
    nmeteors = models.BigIntegerField(blank=True, null=True)
    class Meta:
        db_table = 'meteors_nights_view'
        app_label = 'favor2'

class MeteorsNightsImagesView(models.Model):
    night = models.TextField(primary_key=True)
    nmeteors = models.BigIntegerField(blank=True, null=True)
    nimages = models.BigIntegerField(blank=True, null=True)
    class Meta:
        db_table = 'meteors_nights_images_view'
        app_label = 'favor2'

class MeteorsNightsProcessed(models.Model):
    night = models.TextField(primary_key=True)
    comments = models.TextField(blank=True, null=True)
    params = DictionaryField()
    class Meta:
        db_table = 'meteors_nights_processed'
        app_label = 'favor2'

class MeteorsShowers(models.Model):
    id = models.IntegerField(primary_key=True)
    iaucode = models.TextField(blank=True)
    name = models.TextField(blank=True)
    activity = models.TextField(blank=True)
    status = models.IntegerField(blank=True, null=True)
    solar_lon = models.FloatField(blank=True, null=True)
    ra = models.FloatField(blank=True, null=True)
    dec = models.FloatField(blank=True, null=True)
    class Meta:
        db_table = 'meteors_showers'
        app_label = 'favor2'

class SurveyTransients(models.Model):
    id = models.IntegerField(primary_key=True)
    channel_id = models.IntegerField(blank=True, null=True)
    frame = models.ForeignKey(Images, blank=True, null=True)
    time = models.DateTimeField(blank=True, null=True)
    night = models.TextField(blank=True)
    filter = models.ForeignKey(Filters, db_column='filter', blank=True, null=True)
    ra = models.FloatField(blank=True, null=True)
    dec = models.FloatField(blank=True, null=True)
    mag = models.FloatField(blank=True, null=True)
    mag_err = models.FloatField(blank=True, null=True)
    flux = models.FloatField(blank=True, null=True)
    flux_err = models.FloatField(blank=True, null=True)
    x = models.FloatField(blank=True, null=True)
    y = models.FloatField(blank=True, null=True)
    flags = models.IntegerField(blank=True, null=True)
    preview = models.TextField(blank=True)
    simbad = models.TextField(blank=True)
    mpc = models.TextField(blank=True)
    params = DictionaryField()
    class Meta:
        db_table = 'survey_transients'
        app_label = 'favor2'

class Alerts(models.Model):
    id = models.IntegerField(primary_key=True)
    time = models.DateTimeField(blank=True, null=True)
    night = models.TextField(blank=True)
    channel_id = models.IntegerField(blank=True, null=True)
    ra = models.FloatField(blank=True, null=True)
    dec = models.FloatField(blank=True, null=True)
    excess = models.FloatField(blank=True, null=True)
    class Meta:
        db_table = 'alerts'
        app_label = 'favor2'
