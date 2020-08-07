# -*- coding: utf-8 -*-
"""
    Model backend that enables permissions for AnonymusUsers.

    I wanted it to be as simple as possible so anonymous users just forward their permission checks
    to some fixed user model. This instance can be edited via django admin, assigned to groups, etc.

    To control which user will represent anonymous user you use ANONYMOUS_USER_NAME setting in
    settings file.

    To provide some sensible level of security i enforce following for user that represents
    anonymous user:

        * This user must have password equal to UNUSABLE_PASSWORD
        * This user may not log in
        * You cant cange password for this user via admin.

    You need to enable this backend by setting AUTHENTICATION_BACKENDS. Please note that you
    should not place this backend alongside django ModelBackend. This backend inherits from it.
"""
__author__ = 'jb'

from django.conf import settings
from django.contrib.auth.models import User, Permission
from django.contrib.contenttypes.models import ContentType
from django.db.models.signals import pre_save
from django.dispatch.dispatcher import receiver

from django.contrib.auth.backends import ModelBackend

import logging
_LOGGER = logging.getLogger(__name__)

def get_anon_user_name():
    return getattr(settings, "ANONYMOUS_USER_NAME", None)

class AnonymousUserBackend(ModelBackend):

    def get_all_permissions(self, user_obj, obj=None):
        if user_obj.is_anonymous():
                if not hasattr(user_obj, '_perm_cache'):
                    anon_user_name =  get_anon_user_name()
                    anon_user, created = User.objects.get_or_create(username = anon_user_name)
                    anon_user.set_unusable_password()
                    user_obj._perm_cache = self.get_all_permissions(anon_user, obj)
                return user_obj._perm_cache
        return super(AnonymousUserBackend, self).get_all_permissions(user_obj, obj)

    def authenticate(self, username=None, password=None):
        if username == get_anon_user_name():
            return False
        return super(AnonymousUserBackend, self).authenticate(username, password)

    def has_perm(self, user_obj, perm, obj=None):
        if not user_obj.is_active and not user_obj.is_anonymous:
            return False

        return perm in self.get_all_permissions(user_obj, obj)

@receiver(pre_save, sender=User)
def disable_anon_user_password_save(sender, **kwargs):
    instance = kwargs['instance']
    if instance.username == get_anon_user_name() and instance.has_usable_password():
        raise ValueError("Can't set anonymous user password to something other than unusable password")

def register_custom_permissions(permissions, appname):
    ct, created = ContentType.objects.get_or_create(model='', app_label=appname,
                                                        defaults={'name': appname})

    for codename, name in permissions:
        p, created = Permission.objects.get_or_create(codename=codename,
                        content_type__pk=ct.id,
                        defaults={'name': name, 'content_type': ct})
