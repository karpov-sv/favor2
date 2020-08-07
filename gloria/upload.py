#!/usr/bin/env python

import psycopg2, psycopg2.extras
import os
import posixpath
import urllib, urllib2
import json
import tempfile
import shutil
import fitsimage
import os
import pyfits
import numpy as np
import scipy.ndimage as ndimage
import cv2

def fix_remote_path(filename, channel_id=0):
    if not posixpath.exists(filename) and channel_id:
        return posixpath.abspath(posixpath.join('/MMT/%d' % channel_id, filename))
    else:
        return filename

class GloriaUploader:
    basepath = '.'
    hostname = 'favor2.info'
    hostpath = 'WWW/gloria/images'
    dbhost = ''

    baseapi = 'http://favor2.info:8880'

    def __init__(self, dbname="favor2", dbhost='', host="favor2.info", path="WWW/gloria/images", base=".", api="", channel_id=0):
        connstring = "dbname=" + dbname
        if dbhost:
            connstring += " host="+dbhost

        self.conn = psycopg2.connect(connstring)
        self.conn.autocommit = True
        self.basepath = base
        self.hostname = host
        self.hostpath = path
        self.baseapi = api

        self.is_laser = False

    def query(self, string="", data=(), simplify=True, debug=False):
        cur = self.conn.cursor(cursor_factory = psycopg2.extras.DictCursor)

        if debug:
            print cur.mogrify(string, data)

        cur.execute(string, data)
        try:
            result = cur.fetchall()
            # Simplify the result if it is simple
            if simplify and len(result) == 1:
                if len(result[0]) == 1:
                    return result[0][0]
                else:
                    return result[0]
            else:
                return result
        except:
            # Nothing returned from the query
            return None

    def gloriaQuery(self, string=""):
        res = urllib2.urlopen(self.baseapi + string).read()

        return json.loads(res)

    def uploadImage(self, filename, external_id = 0, type='raw'):
        shortname = posixpath.split(filename)[-1]
        fullname = posixpath.join(self.basepath, filename)
        hostpath = posixpath.join(self.hostpath, shortname)
        print "Uploading file %s to %s:%s" % (fullname, self.hostname, hostpath)

        if os.system("rsync -avzP %s %s:%s" % (fullname, self.hostname, hostpath)) == 0:
            #print self.baseapi + '/api/add_image?id=%d&filename=%s' % (external_id, urllib.quote(shortname))
            res = json.loads(urllib2.urlopen(self.baseapi + '/api/add_image?id=%d&filename=%s&type=%s' % (external_id, urllib.quote(shortname), urllib.quote(type))).read())

            if res['ret'] > 0:
                return True

        return False

    def getCompletedPlans(self):
        plans = self.query("SELECT * FROM scheduler_targets WHERE status = get_scheduler_target_status_id('completed') AND type='GLORIA';", simplify=False)

        return plans

    def completePlan(self, id=0, external_id=0):
        print "Marking plan %d / %d as completed" % (id, external_id)

        urllib2.urlopen(self.baseapi + '/api/complete?id=%d' % (external_id))

        self.query("UPDATE scheduler_targets SET status=get_scheduler_target_status_id('archived') WHERE id=%s; ", (id,))

    def restartPlan(self, id=0):
        print "Restarting plan %d" % (id)

        self.query("UPDATE scheduler_targets SET status=get_scheduler_target_status_id('active') WHERE id=%s; ", (id,))

    def planExistsInGLORIA(self, external_id=0):
        result = False

        try:
            res = self.gloriaQuery('/api/status?id=%d' % (external_id))

            if res['ret'] > 0:
                result = True
        except:
            pass

        return result

    def checkLaser(self, filename):
        return True

        img = pyfits.getdata(filename)
        size = max(img.shape)

        mask = ndimage.gaussian_filter(img, 1)
        thresh = np.percentile(mask, 95)
        _,mask = cv2.threshold(mask.astype(np.float32), thresh, 1, cv2.THRESH_BINARY)
        lines = cv2.HoughLines(mask.astype(np.uint8), 1, np.pi*1/180, int(0.5*size))

        if lines != None and lines.shape[1]:
            print "%s: probably laser" % filename
            return False

        return True

    def uploadPlanImages(self, uuid='', external_id=0):
        images = self.query("SELECT * FROM images WHERE keywords->'TARGET UUID' = %s ORDER BY channel_id ASC, time DESC;", (uuid,), simplify=False)

        self.is_laser = False

        for image in images:
            # First usable image
            filename = fix_remote_path(image['filename'], image['channel_id'])

            if not self.checkLaser(filename):
                self.is_laser = True
                continue
            else:
                self.is_laser = False

            if self.uploadImage(filename, external_id, 'raw'):
                return True

        return False

    def operate(self):
        plans = self.getCompletedPlans()

        for plan in plans:
            print "Uploading plan %d / %d: %s" % (plan['id'], plan['external_id'], plan['name'])

            if self.planExistsInGLORIA(plan['external_id']):
                if self.uploadPlanImages(uuid=plan['uuid'], external_id=plan['external_id']):
                    print "Images successfully uploaded, completing the plan"
                    self.completePlan(plan['id'], plan['external_id'])
                else:
                    if self.is_laser:
                        print "Only laser images, should restart the plan"
                        self.restartPlan(plan['id'])
                    else:
                        print "Upload failed, will try later"
            else:
                print "No such plan in GLORIA, completing it anyway"
                self.completePlan(plan['id'], plan['external_id'])

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-d', '--dbname', help='database, defaults to favor2', action='store', dest='dbname', default="favor2")
    parser.add_option('--dbhost', help='database, defaults to localhost', action='store', dest='dbhost', default="")
    parser.add_option('-H', '--host', help='host name, defaults to favor2.info', action='store', dest='hostname', default="favor2.info")
    parser.add_option('-p', '--hostpath', help='host path, defaults to WWW/gloria/images', action='store', dest='hostpath', default="WWW/gloria/images")
    parser.add_option('-b', '--base', help='base path, defaults to current dir', action='store', dest='basename', default="")
    parser.add_option('-a', '--api', help='base API url, defaults to http://favor2.info:8880', action='store', dest='baseapi', default="http://favor2.info:8880")

    (options,args) = parser.parse_args()

    up = GloriaUploader(dbname=options.dbname, dbhost=options.dbhost, host=options.hostname, path=options.hostpath, base=options.basename, api=options.baseapi)

    up.operate()
