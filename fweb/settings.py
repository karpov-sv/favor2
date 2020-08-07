# Django settings for fweb project.

# DEBUG = False
DEBUG = True
TEMPLATE_DEBUG = DEBUG

ALLOWED_HOSTS = '*'

ADMINS = (
    # ('Your Name', 'your_email@example.com'),
)

MANAGERS = ADMINS

DATABASES = {
    # Everything Django-related
    'default': {
        'ENGINE': 'django.db.backends.postgresql_psycopg2', # Add 'postgresql_psycopg2', 'mysql', 'sqlite3' or 'oracle'.
        'NAME': 'favor2web',                      # Or path to database file if using sqlite3.
        'USER': 'karpov',                      # Not used with sqlite3.
        'PASSWORD': '',                  # Not used with sqlite3.
        'HOST': '',                      # Set to empty string for localhost. Not used with sqlite3.
        'PORT': '',                      # Set to empty string for default. Not used with sqlite3.
        'HAS_HSTORE': False,
    },
    # Observational data db, should not be used for Django objects
    'favor2': {
        'ENGINE': 'django.db.backends.postgresql_psycopg2', # Add 'postgresql_psycopg2', 'mysql', 'sqlite3' or 'oracle'.
        'NAME': 'favor2',                      # Or path to database file if using sqlite3.
        'USER': 'karpov',                      # Not used with sqlite3.
        'PASSWORD': '',                  # Not used with sqlite3.
        'HOST': '',                      # Set to empty string for localhost. Not used with sqlite3.
        'PORT': '',                      # Set to empty string for default. Not used with sqlite3.
    }
}

DATABASE_ROUTERS = ['fweb.routers.Favor2Router']

# Local time zone for this installation. Choices can be found here:
# http://en.wikipedia.org/wiki/List_of_tz_zones_by_name
# although not all choices may be available on all operating systems.
# In a Windows environment this must be set to your system time zone.
TIME_ZONE = 'UTC'

DATETIME_FORMAT = 'Y-m-d H:i:sO'

DATE_FORMAT = 'Y-m-d'

# Language code for this installation. All choices can be found here:
# http://www.i18nguy.com/unicode/language-identifiers.html
LANGUAGE_CODE = 'en-us'

TEST_RUNNER = 'django.test.runner.DiscoverRunner'

SITE_ID = 1

# If you set this to False, Django will make some optimizations so as not
# to load the internationalization machinery.
USE_I18N = False

# If you set this to False, Django will not format dates, numbers and
# calendars according to the current locale.
USE_L10N = True

# If you set this to False, Django will not use timezone-aware datetimes.
USE_TZ = False

# Absolute filesystem path to the directory that will hold user-uploaded files.
# Example: "/home/media/media.lawrence.com/media/"
MEDIA_ROOT = ''

# URL that handles the media served from MEDIA_ROOT. Make sure to use a
# trailing slash.
# Examples: "http://media.lawrence.com/media/", "http://example.com/media/"
MEDIA_URL = ''

# Absolute path to the directory static files should be collected to.
# Don't put anything in this directory yourself; store your static files
# in apps' "static/" subdirectories and in STATICFILES_DIRS.
# Example: "/home/media/media.lawrence.com/static/"
STATIC_ROOT = 'static'

# URL prefix for static files.
# Example: "http://media.lawrence.com/static/"
STATIC_URL = '/static/'

# Additional locations of static files
STATICFILES_DIRS = (
    # Put strings here, like "/home/html/static" or "C:/www/django/static".
    # Always use forward slashes, even on Windows.
    # Don't forget to use absolute paths, not relative paths.
)

# List of finder classes that know how to find static files in
# various locations.
STATICFILES_FINDERS = (
    'django.contrib.staticfiles.finders.FileSystemFinder',
    'django.contrib.staticfiles.finders.AppDirectoriesFinder',
#    'django.contrib.staticfiles.finders.DefaultStorageFinder',
)

# Make this unique, and don't share it with anybody.
SECRET_KEY = 'm%pj!899y52r25#ra$m!k$to!ltsv8&amp;=8&amp;r-2u#y%59&amp;i^lu4p'

# List of callables that know how to import templates from various sources.
TEMPLATE_LOADERS = (
    'django.template.loaders.filesystem.Loader',
    'django.template.loaders.app_directories.Loader',
#     'django.template.loaders.eggs.Loader',
)

MIDDLEWARE_CLASSES = (
    'django.middleware.common.CommonMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    #'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
    # Uncomment the next line for simple clickjacking protection:
    # 'django.middleware.clickjacking.XFrameOptionsMiddleware',
)

ROOT_URLCONF = 'fweb.urls'

# Python dotted path to the WSGI application used by Django's runserver.
WSGI_APPLICATION = 'fweb.wsgi.application'

TEMPLATE_DIRS = (
    # Put strings here, like "/home/html/django_templates" or "C:/www/django/templates".
    # Always use forward slashes, even on Windows.
    # Don't forget to use absolute paths, not relative paths.
    # '/home/karpov/tmp/fweb/fweb/templates'
)

INSTALLED_APPS = (
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    # 'django.contrib.sites',
    'django.contrib.messages',
    'django.contrib.staticfiles',
    # Uncomment the next line to enable the admin:
    'django.contrib.admin',
    # Uncomment the next line to enable admin documentation:
    # 'django.contrib.admindocs',
    'django_hstore',
    'fweb',
    'favor2',
    'endless_pagination',
    'fweb.templatetags.filters'
)

# A sample logging configuration. The only tangible logging
# performed by this configuration is to send an email to
# the site admins on every HTTP 500 error when DEBUG=False.
# See http://docs.djangoproject.com/en/dev/topics/logging for
# more details on how to customize your logging configuration.
LOGGING = {
    'version': 1,
    'disable_existing_loggers': False,
    'filters': {
        'require_debug_false': {
            '()': 'django.utils.log.RequireDebugFalse'
        }
    },
    'handlers': {
        'mail_admins': {
            'level': 'ERROR',
            'filters': ['require_debug_false'],
            'class': 'django.utils.log.AdminEmailHandler'
        }
    },
    'loggers': {
        'django.request': {
           'handlers': ['mail_admins'],
#            'handlers': [],
            'level': 'ERROR',
            'propagate': True,
        },
    }
}

from django.conf.global_settings import TEMPLATE_CONTEXT_PROCESSORS
TEMPLATE_CONTEXT_PROCESSORS += (
    'django.core.context_processors.request',
)

# Endless pagination
ENDLESS_PAGINATION_PAGE_LIST_CALLABLE = 'endless_pagination.utils.get_elastic_page_numbers'

AUTHENTICATION_BACKENDS = ('fweb.anonymoususerbackend.AnonymousUserBackend', )
ANONYMOUS_USER_NAME = 'anonymous'

#### Favor2 settings
BASE = '.'
BASE_RAW = 'IMG'
BASE_ARCHIVE = 'ARCHIVE'

#LICENSED = 'Kazan Federal University'

SHOW_BEHOLDER = True

SHOW_CHANNEL = False
CHANNEL_ID = 0

SHOW_MOUNT = False
MOUNT_ID = 0

SHOW_RAW = True

# Number of channels
NCHANNELS = 9

# Frame parameters
IMAGE_WIDTH = 2560
IMAGE_HEIGHT = 2160

# Add all local variables to template context
_context = {}
local_context = locals()
for (k,v) in local_context.items():
    _context[k] = str(v)

def settings_context(context):
    return _context

TEMPLATE_CONTEXT_PROCESSORS += (
    'fweb.settings.settings_context',
)

DJANGO_HSTORE_ADAPTER_REGISTRATION = 'connection'

INSTALLED_APPS += (
    'debug_toolbar',
    'template_timings_panel',
    'debug_toolbar_line_profiler',
)

DEBUG_TOOLBAR_PANELS = [
    'debug_toolbar.panels.versions.VersionsPanel',
    'debug_toolbar.panels.timer.TimerPanel',
    'debug_toolbar.panels.settings.SettingsPanel',
    'debug_toolbar.panels.headers.HeadersPanel',
    'debug_toolbar.panels.request.RequestPanel',
    'debug_toolbar.panels.sql.SQLPanel',
    'debug_toolbar.panels.staticfiles.StaticFilesPanel',
    'debug_toolbar.panels.templates.TemplatesPanel',
    'debug_toolbar.panels.cache.CachePanel',
    'debug_toolbar.panels.signals.SignalsPanel',
    'debug_toolbar.panels.logging.LoggingPanel',
    'debug_toolbar.panels.redirects.RedirectsPanel',
    'debug_toolbar.panels.profiling.ProfilingPanel',
    'template_timings_panel.panels.TemplateTimings.TemplateTimings',
    'debug_toolbar_line_profiler.panel.ProfilingPanel',
]

# Fix DEBUG_TOOLBAR autodiscover()
try:
    import sys
    if sys.argv[1] == 'runserver' or sys.argv[1] == 'runserver_plus':
        DEBUG_TOOLBAR_PATCH_SETTINGS = DEBUG
    else:
        DEBUG_TOOLBAR_PATCH_SETTINGS = False
except IndexError:
        DEBUG_TOOLBAR_PATCH_SETTINGS = False

CACHES = {
    'default': {
        #'BACKEND': 'django.core.cache.backends.dummy.DummyCache',
        'BACKEND': 'django.core.cache.backends.filebased.FileBasedCache',
        'LOCATION': '/var/tmp/django_cache',
        #'BACKEND': 'django.core.cache.backends.locmem.LocMemCache',
        'OPTIONS': {
            'MAX_ENTRIES': 10000
        }
    }
}

# Site-wise cache
#MIDDLEWARE_CLASSES = ('django.middleware.cache.UpdateCacheMiddleware',) + MIDDLEWARE_CLASSES + ('django.middleware.cache.FetchFromCacheMiddleware',)
#CACHE_MIDDLEWARE_ALIAS = 'default'
#CACHE_MIDDLEWARE_SECONDS = 300

LATITUDE = 43.65722222
LONGITUDE = 41.43235417
ELEVATION = 2065
