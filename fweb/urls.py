from django.http import HttpResponse
from django.contrib.staticfiles.urls import staticfiles_urlpatterns
from django.conf.urls import patterns, include, url
from django.contrib import admin

import settings

import views
import views_logs
import views_scheduler
import views_images
import views_raw
import views_realtime
import views_nights
import views_secondscale
import views_survey
import views_calibration
import views_tasks
import views_sql
import views_status
import views_comments
import views_meteors
import views_satellites
import views_markdown
import views_alerts

import views_errors

admin.autodiscover()

urlpatterns = patterns('',
                       # List of nights with some statistical info
                       url(r'^nights/?$', views_nights.nights_list, {'all' : False}, name='nights'),
                       url(r'^nights/all/?$', views_nights.nights_list, {'all' : True}, name='nights_all'),
                       url(r'^nights/summary/(?P<night>\w+)?$', views_nights.night_summary, name='night_summary'),
                       url(r'^nights/moving/latest$', views_nights.nights_moving, {'age' : 1800}, name='nights_moving'),
                       url(r'^nights/moving/(?P<night>\w+)?$', views_nights.nights_moving, name='nights_moving'),
                       url(r'^moving/(?P<id>\d+)?$', views_nights.download_moving, name='download_moving'),
                       # Log view
                       url(r'^logs(/(?P<id>\w+)?)?$', views_logs.logs_list, name='logs'),
                       # Positional search
                       url(r'^images/coords/?$', views_images.images_list, name='images_coords_search'),
                       url(r'^images/coords/(?P<ra>[^/]+)/(?P<dec>[^/]+)(/(?P<sr>[^/]+)?)?(/type/(?P<type>\w+))?$', views_images.images_list, name='images_coords'),
                       # Detailed image view
                       url(r'^images/(?P<id>\d+)/?$', views_images.image_details, name='image'),
                       # List of images with previews
                       url(r'^images(/(night/(?P<night>\w+))?)?(/(?P<type>\w+))?$', views_images.images_list, name='images'),
                       # Viewing and downloading image itself
                       url(r'^images/(?P<id>\d+)/view', views_images.image_processed, {'size' : 800}, name="image_view"),
                       url(r'^images/(?P<id>\d+)/preview', views_images.image_processed, {'size' : 64}, name="image_preview"),
                       url(r'^images/(?P<id>\d+)/full', views_images.image_processed, {'size' : 0}, name="image_view"),
                       url(r'^images/(?P<id>\d+)/download$', views_images.image_download, name="image_download"),
                       url(r'^images/(?P<id>\d+)/download/processed$', views_images.image_processed, {'format':'fits'}, name="image_download_processed"),
                       url(r'^images/(?P<id>\d+)/histogram', views_images.image_histogram, name="image_histogram"),
                       url(r'^images/(?P<id>\d+)/slice', views_images.image_slice, name="image_slice"),
                       url(r'^images/(?P<id>\d+)/raw', views_images.image_processed, {'size' : 0, 'raw':True}, name="image_view"),
                       # Scheduler
                       url(r'^scheduler/?$', views_scheduler.scheduler_targets_list, name='scheduler_targets'),
                       url(r'^scheduler/(?P<id>\d+)$', views_scheduler.scheduler_target_view, name='scheduler_target'),
                       url(r'^scheduler/(?P<id>\d+)/status/?$', views_scheduler.scheduler_target_status, name='scheduler_target_status'),
                       url(r'^scheduler/(?P<id>\d+)/rgb/?$', views_scheduler.scheduler_target_rgb_image, name='scheduler_target_rgb'),
                       url(r'^scheduler/(?P<id>\d+)/lvc/?$', views_scheduler.scheduler_target_lvc_image, name='scheduler_target_lvc'),
                       url(r'^scheduler/map/?$', views_scheduler.scheduler_map, {'size':600}, name='scheduler_map'),
                       # RAW data
                       url(r'^raw(/(?P<channel>\d+)?)?/?$', views_raw.raw_list, name="raw_list"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<dir>\d{8}_\d{6})/?$', views_raw.raw_dir, name="raw_dir"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)$', views_raw.raw_view, {'type' : 'fits'}, name="raw_download"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/preview$', views_raw.raw_view, {'size' : 128, 'processed' : True}, name="raw_view"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/view$', views_raw.raw_view, {'size' : 800, 'processed' : True}, name="raw_view"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/full$', views_raw.raw_view, {'size' : 0, 'processed' : True}, name="raw_view_full"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/raw$', views_raw.raw_view, {'size' : 0, 'processed' : False}, name="raw_view_raw"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/info$', views_raw.raw_info, name="raw_info"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/histogram$', views_raw.raw_histogram, name="raw_histogram"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/slice$', views_raw.raw_slice, name="raw_slice"),
                       url(r'^raw(/(?P<channel>\d+))?/(?P<filename>\d{8}_\d{6}/.*\.fits)/mosaic$', views_raw.raw_mosaic, name="raw_mosaic"),
                       # Realtime
                       url(r'^realtime/?$', views_realtime.realtime_objects_list, name='realtime_all'),
                       url(r'^realtime/night/(?P<night>\d{4}_\d{2}_\d{2})(/(?P<state>\w+)?)?$', views_realtime.realtime_objects_list, name='realtime_objects'),
                       url(r'^realtime(/(?P<state>\w+)?)?$', views_realtime.realtime_objects_list, name='realtime_all_state'),
                       url(r'^realtime/object/(?P<object_id>\d+)/?$', views_realtime.realtime_object_details, name='realtime_object'),
                       url(r'^realtime/object/(?P<object_id>\d+)/lightcurve$', views_realtime.realtime_object_lightcurve, {'size' : 800}, name='realtime_object_lightcurve'),
                       url(r'^realtime/object/(?P<object_id>\d+)/magnitudes$', views_realtime.realtime_object_lightcurve, {'size' : 800, 'type' : 'mags'}, name='realtime_object_magnitudes'),
                       url(r'^realtime/object/(?P<object_id>\d+)/map$', views_realtime.realtime_object_map, {'size' : 800}, name='realtime_object_map'),
                       url(r'^realtime/object/(?P<object_id>\d+)/animation$', views_realtime.realtime_object_animation, name='realtime_object_animation'),
                       url(r'^realtime/image/(?P<image_id>\d+)/preview$', views_realtime.realtime_image_view, {'size' : 100}, name="realtime_image_preview"),
                       url(r'^realtime/image/(?P<image_id>\d+)/preview_large$', views_realtime.realtime_image_view, {'size' : 800}, name="realtime_image_preview_large"),
                       url(r'^realtime/image/(?P<image_id>\d+)/view$', views_realtime.realtime_image_view, {'size' : 0}, name="realtime_image_view"),
                       url(r'^realtime/image/(?P<image_id>\d+)/download$', views_realtime.realtime_image_view, {'type' : 'fits'}, name="realtime_image_download"),
                       # Realtime Alerts
                       url(r'^alerts/?$', views_alerts.alerts_list, name='alerts_all'),
                       url(r'^alerts/map/?$', views_alerts.alerts_map, name='alerts_map_all'),
                       url(r'^alerts/night/(?P<night>\d{4}_\d{2}_\d{2})/?$', views_alerts.alerts_list, name='alerts_night'),
                       url(r'^alerts/night/(?P<night>\d{4}_\d{2}_\d{2})/map/?$', views_alerts.alerts_map, name='alerts_map'),
                       url(r'^alerts/(?P<id>\d+)/?$', views_alerts.alert_details, name='alert_details'),
                       # Secondscale
                       # url(r'^secondscale/coords/?$', views_secondscale.secondscale_info, name='secondscale_info_coords_search'),
                       # url(r'^secondscale/coords/(?P<ra>[^/]+)/(?P<dec>[^/]+)(/(?P<sr>[^/]+)?)?$', views_secondscale.secondscale_info, name='secondscale_info_coords'),
                       # url(r'^secondscale/lightcurve/coords/?$', views_secondscale.secondscale_lightcurve, name='secondscale_lightcurve_coords_search'),
                       # url(r'^secondscale/lightcurve/coords/(?P<ra>[^/]+)/(?P<dec>[^/]+)(/(?P<sr>[^/]+)?)(/(?P<type>(mag|flux)))?$', views_secondscale.secondscale_lightcurve, name='secondscale_lightcurve_coords'),
                       # url(r'^secondscale/map/coords/?$', views_secondscale.secondscale_map, name='secondscale_map_coords_search'),
                       # url(r'^secondscale/map/coords/(?P<ra>[^/]+)/(?P<dec>[^/]+)(/(?P<sr>[^/]+)?)?$', views_secondscale.secondscale_map, name='secondscale_map_coords'),
                       # Survey
                       url(r'^survey/?$', views_survey.survey_info, name='survey_info'),
                       url(r'^survey/image/(?P<id>\d+)/(?P<ra>[^/]+)/(?P<dec>[^/]+)/(?P<sr>[^/]+)?/download?$', views_survey.survey_image_download, name='survey_image_download'),
                       url(r'^survey/image/(?P<id>\d+)/(?P<ra>[^/]+)/(?P<dec>[^/]+)/(?P<sr>[^/]+)?/?$', views_survey.survey_image, name='survey_image'),
                       url(r'^survey/lightcurve/(?P<ra>[^/]+)/(?P<dec>[^/]+)/(?P<sr>[^/]+)?/?$', views_survey.survey_lightcurve, name='survey_lightcurve'),
                       url(r'^survey/transients/?$', views_survey.survey_transients_list, name='survey_transients_list'),
                       url(r'^survey/transients/night/(?P<night>\d{4}_\d{2}_\d{2})/?$', views_survey.survey_transients_list, name='survey_transients_night_list'),
                       url(r'^survey/transients/(?P<id>\d+)/?$', views_survey.survey_transient_details, name='survey_transient_details'),
                       url(r'^survey/transients/(?P<id>\d+)/image$', views_survey.survey_transient_image, name='survey_transient_image'),
                       url(r'^survey/dbg/image/(?P<id>\d+)/?$', views_survey.survey_dbg_image, name='survey_dbg_image'),
                       url(r'^survey/photometry/?$', views_survey.survey_photometry, name='survey_photometry'),
                       url(r'^survey/(?P<ra>[^/]+)/(?P<dec>[^/]+)/(?P<sr>[^/]+)/?$', views_survey.survey_info, name='survey_info_coords'),
                       # Misc
                       url(r'^/?$', views.index, name="index"),
                       url(r'^search/?$', views.search, name="search"),
                       url(r'^links/?$', views.links, name="links"),
                       # Calibration info
                       url(r'^calibration/?$', views_calibration.calibration, name="calibration"),
                       url(r'^calibration/celostate_dist$', views_calibration.calibration_celostate, {'type':'dist'}, name="calibration_dist"),
                       url(r'^calibration/celostate_shift$', views_calibration.calibration_celostate, {'type':'shift'}, name="calibration_shift"),
                       # Celery tasks
                       url(r'^tasks/?$', views_tasks.tasks_view, name="tasks_list"),
                       url(r'^tasks/(?P<id>[^/]+)/?$', views_tasks.tasks_view, name="tasks_task"),
                       # SQL queries
                       url(r'^sql/?$', views_sql.sql_view, name="sql"),
                       url(r'^sql/unsafe/?$', views_sql.sql_view, {'readonly':False}, name="sql_unsafe"),
                       # Beholder status
                       url(r'^status(/(?P<hours>[0-9.]+))?(/(?P<delay>[0-9.]+))?/?$', views_status.status_view, name="status"),
                       url(r'^status/custom/(?P<param>[a-z0-9_]+)/(?P<hours>[0-9.]+)/(?P<delay>[0-9.]+)/?$', views_status.status_custom_plot, name="status_custom_plot"),
                       url(r'^status/channels/(?P<param>[a-z0-9_]+)/(?P<hours>[0-9.]+)/(?P<delay>[0-9.]+)/?$', views_status.status_plot, {}, name="status_channels_plot"),
                       url(r'^status/can/(?P<param>[a-z0-9_]+)/(?P<hours>[0-9.]+)/(?P<delay>[0-9.]+)/?$', views_status.status_plot, {'mode':'can'}, name="status_can_plot"),
                       url(r'^status/beholder/?$', views_status.status_beholder, name='status_beholder'),
                       url(r'^status/beholder/sky/?$', views_status.status_beholder_sky, name='status_beholder_sky'),
                       # Comments
                       url(r'^comments(/(?P<state>\w+)?)?$', views_comments.comments_view, name="comments"),
                       # Meteors
                       url(r'^meteors/?$', views_meteors.meteors_nights, name='meteors_nights'),
                       url(r'^meteors/(?P<id>\d+)/?$', views_meteors.meteor_details, name='meteor_details'),
                       url(r'^meteors/(?P<id>\d+)/download/?$', views_meteors.meteor_download_track, name='meteor_download_track'),
                       url(r'^meteors/(?P<id>\d+)/plot/(?P<type>\w+)/?$', views_meteors.meteor_plot, {'size' : 800}, name='meteor_plot'),
                       url(r'^meteors/night/(?P<night>\w+)?$', views_meteors.meteors_list, name='meteors_list'),
                       url(r'^meteors/night/(?P<night>\w+)/intersect/?$', views_meteors.meteors_intersect, {'size' : 800}, name='meteors_intersect'),
                       url(r'^meteors/night/(?P<night>\w+)/radiants/?$', views_meteors.meteors_radiants, {'size' : 800}, name='meteors_radiants'),
                       url(r'^meteors/night/(?P<night>\w+)/radiants/normalize?$', views_meteors.meteors_radiants, {'size' : 800, 'normalize' : True}, name='meteors_radiants_normalize'),
                       url(r'^meteors/night/(?P<night>\w+)/map/(?P<ra0>[^/]+)/(?P<dec0>[^/]+)/?$', views_meteors.meteors_map, {'size' : 800}, name='meteors_map'),
                       url(r'^meteors/night/(?P<night>\w+)/plot/(?P<type>\w+)/?$', views_meteors.meteors_plot, {'size' : 800}, name='meteors_night_plot'),
                       url(r'^meteors/night/(?P<night>\w+)/download/?$', views_meteors.meteors_download, name='meteors_download'),
                       url(r'^meteors/plot/(?P<type>\w+)/?$', views_meteors.meteors_plot, {'size' : 800}, name='meteors_plot'),
                       # Satellites
                       url(r'^satellites/?$', views_satellites.satellites_list, name='satellites_list'),
                       url(r'^satellites/download/?$', views_satellites.satellite_download, name='satellite_download_list'),
                       url(r'^satellites/(?P<id>[^/]+)/?$', views_satellites.satellite_details, name='satellite_details'),
                       url(r'^satellites/(?P<id>[^/]+)/plot/(?P<type>[a-z0-9_]+)$', views_satellites.satellite_plot, name='satellite_plot'),
                       url(r'^satellites/track/(?P<track_id>[^/]+)/?$', views_satellites.satellite_details, name='satellite_track'),
                       url(r'^satellites/track/(?P<track_id>[^/]+)/plot/(?P<type>[a-z0-9_]+)$', views_satellites.satellite_track_plot, name='satellite_track_plot'),
                       url(r'^satellites/track/(?P<track_id>[^/]+)/download/?$', views_satellites.satellite_download, name='satellite_download_track'),
                       url(r'^satellites/(?P<id>[^/]+)/download/?$', views_satellites.satellite_download, name='satellite_download'),
                       url(r'^satellites/(?P<id>[^/]+)/download/(?P<param>\w+)/?$', views_satellites.satellite_download, name='satellite_download_param'),

                       # Django stuff
                       url(r'^admin/', include(admin.site.urls)),
                       #url(r'^accounts/login/$', 'django.contrib.auth.views.login', {'template_name': 'admin/login.html'}),
                       url('^accounts/', include('django.contrib.auth.urls')),

                       url(r'^mounts/(?P<id>\d+)/?$', views.mount, name="mounts"),
                       url(r'^channels/(?P<id>\d+)/?$', views.channel, name="channels"),

                       # Robots
                       url(r'^robots.txt$', lambda r: HttpResponse("User-agent: *\nDisallow: /\n", content_type="text/plain")),

                       # Markdown
                       url(r'^about/(?P<path>.*)$', views_markdown.markdown_page, {'base':'about'}, name="markdown"),
)

if settings.SHOW_BEHOLDER:
    urlpatterns += patterns('', url(r'^beholder/?$', views.beholder, name="beholder"))
if settings.SHOW_CHANNEL:
    urlpatterns += patterns('', url(r'^channel/?$', views.channel, name="channel"))
if settings.SHOW_MOUNT:
    urlpatterns += patterns('', url(r'^mount/?$', views.mount, name="mount"))

urlpatterns += staticfiles_urlpatterns()
