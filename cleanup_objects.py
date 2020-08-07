#!/usr/bin/env python

import os
import re
import favor2
import posixpath
import shutil

f = favor2.Favor2()

def check_object(id, path):
    res = f.query('select id from realtime_objects where id=%s', (id, ))

    if not res:
        print "Removing %s" % path

        shutil.rmtree(path, ignore_errors=True)

for root, dirs, files in os.walk('ARCHIVE', topdown=True):
    toremove = []
    for dir in dirs:
        if re.match('^\d+$', dir):
            check_object(int(dir), posixpath.join(root, dir))
            toremove.append(dir)

    for dir in toremove:
        dirs.remove(dir)
