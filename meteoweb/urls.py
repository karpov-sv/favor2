from django.contrib.staticfiles.urls import staticfiles_urlpatterns
from django.conf.urls import patterns, include, url
from django.views.generic import DetailView, ListView
from django.db.models import Count
from models import *
from views import *

urlpatterns = patterns('',
                       # List of nights with some statistical info
                       url(r'^nights/?$', ListView.as_view(
                           queryset=MeteoImages.objects.order_by('-night').values('night').annotate(nimages=Count('night')),
                           context_object_name='nights',
                           template_name='nights.html'),
                           name='nights'),
                       # Log view
                       url(r'^$', 'meteoweb.views.current', name='current'),
                       url(r'^current.jpg$', 'meteoweb.views.current_image', name='current_image'),
                       # List of images with previews
                       url(r'^images(/(night/(?P<night>\w+))?)?(/(?P<type>\w+))?$', ImagesListView.as_view(), name='images_night'),
                       # Detailed image view
                       url(r'^images/(?P<pk>\d+)/$', ImageDetailView.as_view(), name='images'),
                       # Viewing and downloading image itself
                       url(r'^images/(?P<id>\d+)/view', 'meteoweb.views.image_jpeg', {'size' : 800}, name="image_view"),
                       url(r'^images/(?P<id>\d+)/preview', 'meteoweb.views.image_jpeg', {'size' : 64}, name="image_preview"),
                       # Sky Quality Meter
                       url(r'^sqm/(?P<field>\w+)/current.jpg', 'meteoweb.views.current_sqm', {'size': 1000, 'field': 'brightness'}, name='current_sqm'),
                       # Boltwood Cloud Sensor II
                       url(r'^meteo/(?P<field>\w+)/current.jpg', 'meteoweb.views.current_meteo', {'size': 1000}, name='current_sqm'),

                       # Tabular output
                       url(r'^current.txt', 'meteoweb.views.current_table', name='current_table'),
                       
                       # Robots
                       url(r'^robots.txt$', lambda r: HttpResponse("User-agent: *\nDisallow: /", mimetype="text/plain")),
                                              
)

urlpatterns += staticfiles_urlpatterns()
