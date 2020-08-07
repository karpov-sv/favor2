from django import template
from django.template.defaultfilters import stringfilter

import pytz

register = template.Library()

@register.filter(name='fromutc')
def record_flags(time_utc):
    tz = pytz.timezone('UTC')
    return tz.localize(time_utc)
