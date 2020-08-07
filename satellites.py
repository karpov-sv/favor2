#!/usr/bin/env python
import ephem
import urllib, urllib2
import datetime
import posixpath
import os

import numpy as np

def get_night(t):
    et = t - datetime.timedelta(hours=12)
    return "%04d_%02d_%02d" % (et.year, et.month, et.day)

class Satellites:
    sats = []
    base = '.'
    tledir = 'tles'
    tles = None

    def __init__(self, lat=43.65722222, lon=41.43235417, elev=2065, pressure=0):
        self.obs = ephem.Observer()
        self.obs.lat = np.deg2rad(43.65722222)
        self.obs.lon = np.deg2rad(41.43235417)
        self.obs.elevation = 2065
        self.obs.pressure = 0.0

    def getTles(self):
        credentials = {
            'identity' : 'karpov.sv@gmail.com',
            'password' : '1q2w3e4r5t6y7u8i'
        }

        credentials = urllib.urlencode(credentials)

        request = urllib2.Request('https://www.space-track.org/ajaxauth/login', credentials)
        response = urllib2.urlopen(request)
        cookie = response.headers.get('Set-Cookie')

        request = urllib2.Request('https://www.space-track.org/basicspacedata/query/class/tle_latest/ORDINAL/1/EPOCH/%3Enow-30/orderby/NORAD_CAT_ID/format/3le')
        request.add_header('cookie', cookie)

        self.tles = urllib2.urlopen(request).readlines()

        self.saveTles()

    def getFilename(self, time=datetime.datetime.utcnow(), createdir=False):
        night = get_night(time)
        dirname = posixpath.join(self.base, self.tledir)
        filename = posixpath.join(dirname, "%s.tle" % night)

        if createdir:
            try:
                os.makedirs(dirname)
            except:
                pass

        return filename

    def saveTles(self, filename=''):
        if self.tles and len(self.tles):
            if not filename:
                filename = self.getFilename(datetime.datetime.utcnow(), createdir=True)

            f = file(filename, 'w')
            f.writelines(self.tles)
            f.close()

    def readTles(self, filename='', time=datetime.datetime.utcnow()):
        if not filename:
            filename = self.getFilename(time)

        self.tles = file(filename, 'r').readlines()

        if self.tles:
            self.setSatellites()
            return True
        else:
            return False

    def setSatellites(self, nmax=0):
        self.sats = []

        N = len(self.tles)/3
        if nmax:
            N = min(nmax, N)

        for i in xrange(N):
            try:
                if self.tles[3*i+1].find('.99999999') >= 0:
                    continue

                sat = ephem.readtle(self.tles[3*i], self.tles[3*i+1], self.tles[3*i+2])
                sat.compute(self.obs)
                if not sat.neverup:
                    self.sats.append(sat)
            except:
                print "Wrong TLE %d" % (i)

    def getSatellite(self, ra, dec, time, sr=1.0):
        pos = (ra*ephem.pi/180.0, dec*ephem.pi/180.0)

        self.obs.date = time

        if not self.tles:
            self.readTles(time=time)

        if not self.sats:
            self.setSatellites()

        for sat in self.sats:
            sat.compute(self.obs)
            if sat.alt > 0:
                sep = ephem.separation((sat.a_ra, sat.a_dec), pos)*180.0/ephem.pi
                if sep < sr:
                    return sat

        return None

    def getSatellites(self, ra, dec, time, sr=1.0):
        pos = (ra*ephem.pi/180.0, dec*ephem.pi/180.0)
        sats = []

        self.obs.date = time

        if not self.tles:
            self.readTles(time=time)

        if not self.sats:
            self.setSatellites()

        for sat in self.sats:
            sat.compute(self.obs)
            if sat.alt > 0:
                sep = ephem.separation((sat.a_ra, sat.a_dec), pos)*180.0/ephem.pi
                if sep < sr:
                    sats.append(sat)

        return sats

# Main
if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-b', '--base', help='base path, default to ./', action='store', dest='base', default='.')
    parser.add_option('-g', '--get-tles', help='Download TLEs', action='store_true', dest='get_tles', default=False)

    (options, args) = parser.parse_args()

    sats = Satellites()

    if options.get_tles:
        sats.getTles()
