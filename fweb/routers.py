class Favor2Router(object):
    def db_for_read(self, model, **hints):
        if model._meta.app_label == 'favor2':
            return 'favor2'
        return 'default'

    def db_for_write(self, model, **hints):
        if model._meta.app_label == 'favor2':
            return 'favor2'
        return 'default'

    def allow_relation(self, obj1, obj2, **hints):
        # Allow is both in our app
        if obj1._meta.app_label == 'favor2' and obj2._meta.app_label == 'favor2':
            return True
        # Allow if neither is our app
        elif 'favor2' not in [obj1._meta.app_label, obj2._meta.app_label]:
            return True
        return False

    def allow_syncdb(self, db, model):
        if db == 'favor2' or model._meta.app_label == "favor2":
            return False # we're not using syncdb on our legacy database
        else: # but all other models/databases are fine
            return True
