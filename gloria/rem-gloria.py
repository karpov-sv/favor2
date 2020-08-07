#!/usr/bin/env python
import urllib, urllib2
import posixpath
import json
import ephem
import math
import os
import re
import datetime
import MySQLdb
import MySQLdb.cursors

def deg2rad(x):
    return 3.14159265359*x/180.0

def rad2deg(x):
    return 180.0*x/3.14159265359

# All telescope-specific functionality is in this class
class REM:
    def __init__(self):
        self.obs = ephem.Observer()
        # La Silla Observatory
        self.obs.lat = deg2rad(-29.25666667)
        self.obs.long = deg2rad(-70.73)
        self.obs.elev = 2400.0
        self.obs.pressure = 0.0

        self.filename_completed = ['/home/remos/REMOS/desk/LOGS/REMOBS.performed',
                                   '/home/remos/REMOS/desk/SYSREP/REMOBS.performed']

        self.conn = MySQLdb.connect(host="ross.iasfbo.inaf.it", user="gloria", passwd="GLORIA2011", db="REMImgs", cursorclass=MySQLdb.cursors.DictCursor)

    def query(self, string="", data=(), simplify=True):
        cur = self.conn.cursor()
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

    # Return True if the object is observable (+night +restrictions) any time next 24 hours
    def is_observable(self, ra, dec, restrictions={}):
        result = False

        # Create the object, Moon and Sun
        obj = ephem.readdb("Object,f|S,%s,%s,0.0,2000.0" % (ephem.hours(deg2rad(ra)), ephem.degrees(deg2rad(dec))))
        moon = ephem.Moon()
        sun = ephem.Sun()

        date = ephem.now()

        print ra, dec, restrictions

        # Simulate the next 24 hours
        for t in (0.01*x + date for x in xrange(0,100)):
            self.obs.date = t
            obj.compute(self.obs)
            moon.compute(self.obs)
            sun.compute(self.obs)

            # Is it night time?
            if rad2deg(sun.alt) < -10:
                # Now let's check the restrictions, if any
                if (rad2deg(obj.alt) >= restrictions.get('min_altitude', 0.0) and
                    rad2deg(ephem.separation(obj, moon)) >= restrictions.get('min_moon_distance', 0.0) and
                    moon.alt <= restrictions.get('max_moon_altitude', 90.0)):
                    result = True
                    break

        return result

    def airmass(self, alt = 0):
        za = 90 - alt
        if za > 90:
            za = 90

        airmass = 1.0/(math.cos(deg2rad(za)) + 0.50572*(96.07995 - za)**(-1.6364))

        return airmass

    # Generate unique name to identify the plan
    def get_name(self, id=0):
        return "GLORIA_%d_s" % (id)

    def add_target(self, id=0, name="unknown", ra=0.0, dec=0.0, exp=1.0, filter="g", restrictions = {}):
        # Fix object name
        name = re.sub("[^\w\s]", '', name)
        name = re.sub("\s+", '_', name)

        print "Scheduling target %d: ra=%g dec=%g name=%s exp=%g filter=%s" % (id, ra, dec, name, exp, filter)

        cmdname = "/home/remguest/REMOS/newsupport/Merate/bin/sendEntry"
        # Target data: category RA (deg) DEC (deg) Error (arcmin) Equinox SourceName
        trg = "4 %g %g 0.0 2000.0 %s" % (ra, dec, name)
        # Infrared data: IRflag DIT NINT IRfilter
        if filter in ['J', 'H', 'K']:
            fid = {'J':1, 'H':0, 'K':7}.get(filter)
            ir = "1 %g 1 %d" % (exp, fid)
        else:
            ir = "0 0 1 0"
        # Optical data: OptFlag Exptime CameraFocus CCDsensitivity NINT
        if filter in ['g', 'r', 'i', 'z']: # All optical observations are performed simultaneously in all filters
            opt = "1 %g 0 0 1" % (exp)
        else:
            opt = "0 0 0 0 1"
        # PI data: PIName PIInstitute PIE-mail
        PI = "%s GLORIA karpov.sv@gmail.com" % (self.get_name(id),)
        # Observation data: MinAirmass MaxAirmass MinJD MaxJD MaxMoonFraction
        # MaxSeeing PropId Priority Permanent Periodical Period Group FocPos
        res = "0 %g 0 0 1 4.0 0 4 0 0 0 0 0" % (self.airmass(restrictions.get('min_altitude', 0.0)))

        cmd = "%s -t %s -r %s -o %s -p %s -d %s" % (cmdname, trg, ir, opt, PI, res)

        print cmd

        os.system(cmd)

    def is_observed(self, id=0):
        name = self.get_name(id=id)

        for file in self.filename_completed:
            if posixpath.exists(file):
                with open(file, 'r') as file:
                    for line in iter(file):
                        if name in line:
                            return True

        return False

    def get_images(self, id):
        name = self.get_name(id=id)
        images = self.query("SELECT * FROM Obslog WHERE pi_coi = %s AND dithID in (0,99);", (name,), simplify=False)

        for image in images:
            date = image['date_obs'].strftime('%Y%m%d')
            if image['filter'] in ['g', 'r', 'i', 'z']:
                instr = 'Ross'
            else:
                instr = 'Remir'
            image['url'] = 'http://ross.iasfbo.inaf.it/REMDB/fits_retrieve.php?ffile=/%s/ImgsDBArchive/%s/%s.fits.gz' % (instr, date, image['fname'])
            image['preview_url'] = 'http://ross.iasfbo.inaf.it/%s/ImgsDBArchive/%s/%s.fits.gz' % (instr, date, image['fname'])
            image['type'] = 'raw-%s' % image['filter']

        return images

# All GLORIA-specific functionality is here
class GloriaConnector:
    def __init__(self, api=''):
        self.baseapi = api

        self.plans = []
        self.rem = REM()

    def gloriaQuery(self, string=""):
        res = urllib2.urlopen(self.baseapi + string).read()

        jres = json.loads(res)

        if jres['ret'] <= 0:
            print "Query error (code=%d): %s" % (jres['ret'], string)

        return jres

    def loadPlans(self):
        res = self.gloriaQuery('/api/list')

        if res['ret']:
            self.plans = res['plans']
            print "%d plans loaded" % (len(self.plans))

    def approvePlan(self, id=0):
        print "Approving plan %d" % (id)
        res = self.gloriaQuery('/api/approve?id=%d' % (id))

    def rejectPlan(self, id=0):
        print "Rejecting plan %d" % (id)
        res = self.gloriaQuery('/api/reject?id=%d' % (id))

    def acceptPlan(self, id=0):
        print "Accepting plan %d" % (id)
        res = self.gloriaQuery('/api/accept?id=%d' % (id))

    def completePlan(self, id=0):
        print "Completing plan %d" % (id)
        res = self.gloriaQuery('/api/complete?id=%d' % (id))

    def registerImage(self, id=0, url='', preview_url='', type='raw'):
        print "Registering %s image for plan %d: %s" % (type, id, url)
        res = self.gloriaQuery('/api/add_image?' + urllib.urlencode({'id':id, 'type':type, 'url':url, 'preview_url':preview_url}))

    def findNewPlans(self):
        for plan in self.plans:
            # print plan['uuid'], plan['status']

            # 'advertised' plans should be checked for observability and either approved or rejected
            if plan['status'] == 'advertised':
                restrictions={'min_moon_distance':plan['min_moon_distance'],
                              'min_altitude':plan['min_altitude'],
                              'max_moon_altitude':plan['max_moon_altitude']}
                if self.rem.is_observable(plan['ra'], plan['dec'], restrictions=restrictions):
                    self.approvePlan(plan['id'])
                else:
                    self.rejectPlan(plan['id'])

            # 'confirmed' plans should be submitted to REM scheduler
            if plan['status'] == 'confirmed':
                restrictions={'min_moon_distance':plan['min_moon_distance'],
                              'min_altitude':plan['min_altitude'],
                              'max_moon_altitude':plan['max_moon_altitude']}
                self.rem.add_target(plan['id'], plan['name'], plan['ra'], plan['dec'], plan['exposure'], plan['filter'], restrictions=restrictions)
                self.acceptPlan(plan['id'])

            # 'accepted' plans should be checked for completion
            if plan['status'] == 'accepted':
                if self.rem.is_observed(plan['id']):
                    images = self.rem.get_images(plan['id'])
                    # submit urls to resulting images to GLORIA
                    for image in images:
                        if plan['filter'] in ['Clear', image['filter']]:
                            self.registerImage(plan['id'], url=image['url'], preview_url=image['preview_url'], type=image['type'])
                            print image['preview_url'], image['filter']

                    if images:
                        # Plan is done and images are submitted - we mark it as 'completed'
                        self.completePlan(plan['id'])
                        pass

            # we are not interested in other plans, for now

    def operate(self):
        self.loadPlans()

        self.findNewPlans()

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-a', '--api', help='base API url, defaults to http://favor2.info:8881', action='store', dest='baseapi', default="http://favor2.info:8881")

    (options,args) = parser.parse_args()

    conn = GloriaConnector(api=options.baseapi)

    conn.operate()
