# This is an auto-generated Django model module.
# You'll have to do the following manually to clean this up:
#     * Rearrange models' order
#     * Make sure each model has one field with primary_key=True
# Feel free to rename the models, but don't rename db_table values or field names.
#
# Also note: You'll have to insert the output of 'django-admin.py sqlcustom [appname]'
# into your database.

from django.db import models

class MeteoImages(models.Model):
    id = models.IntegerField(primary_key=True)
    filename = models.TextField(blank=True)
    night = models.TextField(blank=True)
    time = models.DateTimeField(null=True, blank=True)
    exposure = models.FloatField(null=True, blank=True)
    class Meta:
        db_table = 'meteo_images'

class MeteoParameters(models.Model):
    id = models.IntegerField(primary_key=True)
    time = models.DateTimeField(null=True, blank=True)
    sensor_temp = models.FloatField(null=True, blank=True)
    sky_ambient_temp = models.FloatField(null=True, blank=True)
    ambient_temp = models.FloatField(null=True, blank=True)
    wind = models.FloatField(null=True, blank=True)
    humidity = models.FloatField(null=True, blank=True)
    dewpoint = models.FloatField(null=True, blank=True)
    rain_flag = models.IntegerField(null=True, blank=True)
    wet_flag = models.IntegerField(null=True, blank=True)
    cloud_cond = models.IntegerField(null=True, blank=True)
    wind_cond = models.IntegerField(null=True, blank=True)
    rain_cond = models.IntegerField(null=True, blank=True)
    day_cond = models.IntegerField(null=True, blank=True)
    class Meta:
        db_table = 'meteo_parameters'

class MeteoSqm(models.Model):
    id = models.IntegerField(primary_key=True)
    time = models.DateTimeField(null=True, blank=True)
    temp = models.FloatField(null=True, blank=True)
    brightness = models.FloatField(null=True, blank=True)
    class Meta:
        db_table = 'meteo_sqm'
