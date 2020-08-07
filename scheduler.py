#!/usr/bin/env python

from gui import immp
from webchannel import Favor2Protocol, Favor2ClientFactory

import healpy as hp
import numpy as np
from coord import ang_sep
import cPickle as pickle
import ephem

import posixpath, datetime, glob, os, sys, tempfile, shutil
from astropy import wcs as pywcs
from astropy.io import fits as pyfits
from favor2 import Favor2, send_email
from gcn_client import plot_LVC_map

# Generic scheduler part

class Scheduler:
    def __init__(self, favor2=None, nside=128, r0=15.0, restore=True, update=True):
        self.favor2 = favor2 or Favor2()

        self.nside = nside
        self.npix = hp.nside2npix(self.nside)

        #print "Map resolution is %g degrees" % (np.rad2deg(hp.nside2resol(self.nside)))

        self.coverage = np.zeros(self.npix)
        self.restrictions = np.zeros(self.npix)
        self.iweights = np.zeros(self.npix)

        theta, phi = hp.pix2ang(nside, np.arange(self.npix))
        self.ra = np.rad2deg(phi)
        self.dec = np.rad2deg(0.5 * np.pi - theta)

        self.r0 = r0

        self.monitoring_exposure = 1000.0

        self.latest_time = datetime.datetime.min + datetime.timedelta(days=1)
        self.update_time = datetime.datetime.min + datetime.timedelta(days=1)
        self.fast_update_time = datetime.datetime.min + datetime.timedelta(days=1)
        self.store_time = datetime.datetime.min + datetime.timedelta(days=1)
        self.altaz_time = datetime.datetime.min + datetime.timedelta(days=1)

        self.interests = {}

        self.modes = {'targets', 'LVC', 'survey'}

        self.update(restore=restore, update=update)

    def update(self, time=None, restore=False, update=False, fast=False):
        time = time or datetime.datetime.utcnow()
        self.favor2.obs.date = time

        if self.favor2.get_night(time) != self.favor2.get_night(self.update_time):
            self.resetCoverage()

        if restore:
            self.restoreCoverage()

        if update:
            self.updateCoverage()

        # Current restrictions based on the Sun, Moon and horizon
        zenith = self.favor2.obs.radec_of(0, np.deg2rad(90))
        self.zenith_ra, self.zenith_dec = np.rad2deg(zenith)
        self.lst = self.zenith_ra # stellar time in degrees

        if (datetime.datetime.utcnow() - self.altaz_time).total_seconds > 60:
            self.alt,self.az = self.get_altaz(self.ra, self.dec)
            self.altaz_time = datetime.datetime.utcnow()

        #print "Zenith at %g %g, stellar time %g" % (self.zenith_ra, self.zenith_dec, st/15.0)

        self.favor2.sun.compute(self.favor2.obs)
        self.favor2.moon.compute(self.favor2.obs)

        self.fast_update_time = time

        if fast:
            return

        #print "Sun is at %g %g, Moon is at %g %g" % (np.rad2deg(self.favor2.sun.ra), np.rad2deg(self.favor2.sun.dec), np.rad2deg(self.favor2.moon.ra), np.rad2deg(self.favor2.moon.dec))

        zdist = ang_sep(self.zenith_ra, self.zenith_dec, self.ra, self.dec)
        sundist = ang_sep(np.rad2deg(self.favor2.sun.ra), np.rad2deg(self.favor2.sun.dec), self.ra, self.dec)
        moondist = ang_sep(np.rad2deg(self.favor2.moon.ra), np.rad2deg(self.favor2.moon.dec), self.ra, self.dec)

        restrict_z = self.restrict_fn(180-zdist, 100+self.r0, 10)
        restrict_moon = self.restrict_fn(moondist, 10+self.r0, 10)
        restrict_sun = self.restrict_fn(sundist, 20+self.r0, 20)

        northdist = ang_sep((self.zenith_ra + 180.0) % 360.0, 50, self.ra, self.dec)
        restrict_north = self.restrict_fn(northdist, 50.0, 10.0)

        restrict_ew = self.restrict_fn(self.alt, 20.0 + 20.0*np.sin(np.deg2rad(self.az))**2, 10)

        self.restrictions = restrict_z*restrict_moon*restrict_sun*restrict_north*restrict_ew
        self.restrictions_weak = restrict_moon*restrict_sun*restrict_north*self.restrict_fn(self.alt, 15.0, 5.0)

        #print "Restrictions updated"

        # Interests
        self.iweights = np.zeros_like(self.coverage)
        for name in self.interests:
            interest = self.interests[name]

            if interest['alt'] or interest['az']:
                # We should update RA/Dec
                interest['ra'], interest['dec'] = np.rad2deg(self.favor2.obs.radec_of(np.deg2rad(interest['az']), np.deg2rad(interest['alt'])))

            self.iweights += self.snapshot_fn(interest['ra'], interest['dec'], interest['radius'], smooth=True)*interest['weight']

        self.updateSatellitePasses(time)

        # Latest update time
        self.update_time = time

    def get_altaz(self, ra, dec):
        ha = self.lst - ra

        alt = np.sin(np.deg2rad(dec))*np.sin(self.favor2.obs.lat) + np.cos(np.deg2rad(dec))*np.cos(self.favor2.obs.lat)*np.cos(np.deg2rad(ha))
        alt = np.rad2deg(np.arcsin(alt))

        az = (np.sin(np.deg2rad(dec)) - np.sin(np.deg2rad(alt))*np.sin(self.favor2.obs.lat))/(np.cos(np.deg2rad(alt))*np.cos(self.favor2.obs.lat))
        az = np.rad2deg(np.arccos(az))

        if np.isscalar(az):
            if np.sin(np.deg2rad(ha)) < 0:
                az = 360.0 - az
        else:
            idx = np.sin(np.deg2rad(ha)) < 0
            az[idx] = 360.0 - az[idx]

        return alt,az

    def updateSatellitePasses(self, time):
        res = self.favor2.query("SELECT * FROM satellite_passes WHERE time > %s + '%s seconds' and time < %s + '%s seconds'", (time, 60.0, time, 60.0 + self.monitoring_exposure), simplify=False)

        for r in res:
            self.iweights += self.snapshot_fn(r['ra'], r['dec'], 3.0, smooth=True)*200.0

    def resetCoverage(self):
        self.coverage = np.zeros(self.npix)
        print "Coverage reset"

    def storeCoverage(self):
        night = self.favor2.get_night(self.latest_time)

        self.favor2.query("UPDATE scheduler_coverage SET coverage=convert_to(%s, 'LATIN1'), time=%s WHERE night=%s", (pickle.dumps(self.coverage), self.latest_time, night))
        self.favor2.query("INSERT INTO scheduler_coverage (night, coverage, time) SELECT %s, convert_to(%s, 'LATIN1'), %s WHERE NOT EXISTS (SELECT 1 FROM scheduler_coverage WHERE night = %s)", (night, pickle.dumps(self.coverage), self.latest_time, night))

        self.store_time = datetime.datetime.utcnow()

        print "Coverage for night %s stored to DB" % night

    def restoreCoverage(self, time=None, update=True):
        time = time or datetime.datetime.utcnow()
        night = self.favor2.get_night(time)
        res = self.favor2.query('SELECT * FROM scheduler_coverage WHERE night=%s', (night,), simplify=False)

        if res:
            self.latest_time = res[0]['time']
            self.coverage = pickle.loads(str(res[0]['coverage']))
            #print "Coverage for night %s restored from DB" % night
        else:
            self.latest_time = datetime.datetime.min
            self.coverage = np.zeros(self.npix)
            #print "No stored coverage for night %s in DB" % night

        if update:
            self.updateCoverage()

    def updateCoverage(self, time=None):
        time = time or datetime.datetime.utcnow()
        night = self.favor2.get_night(time)
        images = self.favor2.query("SELECT * FROM images WHERE type='avg' AND night=%s AND time>%s ORDER BY time", (night, self.latest_time), simplify=False)

        cmap = np.zeros_like(self.coverage)

        for i,image in enumerate(images):
            #print i,len(images)
            ra, dec = image['ra0'], image['dec0']
            sr0 = float(image['keywords']['PIXSCALE'])*0.5*min(image['width'], image['height'])

            cmap += self.snapshot_fn(ra, dec, sr0, smooth=False)
            self.latest_time = image['time']

        if images:
            self.coverage += hp.smoothing(cmap, fwhm=np.deg2rad(self.r0), verbose=False)
            self.storeCoverage()

    def snapshot_fn(self, ra, dec, radius, value=1.0, smooth=False):
        radius = max(radius, 2*np.rad2deg(hp.nside2resol(self.nside)))

        res = np.zeros_like(self.coverage)
        dists = ang_sep(ra, dec, self.ra, self.dec)
        idx = dists < radius
        res[idx] = value

        if smooth:
            res = hp.smoothing(res, fwhm=np.deg2rad(self.r0), verbose=False)

        return res

    def addCoverage(self, ra, dec, sr=5.0, value=1, time=None, store=False):
        time = time or datetime.datetime.utcnow()
        coverage = self.snapshot_fn(ra, dec, sr, value, smooth=True)
        self.coverage += coverage
        self.latest_time = time

        if store:
            self.storeCoverage()

    def restrict_fn(self, r, r_abs, r0):
        res = np.zeros_like(self.coverage)
        idx = (r > r_abs)

        res[idx] = 1 - np.exp(-(r - r_abs)[idx]/r0)

        return res

    def addInterest(self, name, ra, dec, radius=1.0, weight=1.0, reschedule=False, alt=None, az=None):
        interest = {'name': name,
                    'ra': float(ra),
                    'dec': float(dec),
                    'radius': float(radius),
                    'weight': float(weight),

                    'alt': float(alt) if alt else None,
                    'az': float(az) if az else None,

                    'reschedule': reschedule}

        self.interests[name] = interest

    def deleteInterest(self, name):
        self.interests.pop(name, None)

    def pix2radec(self, ipix):
        theta, phi = hp.pix2ang(self.nside, ipix)
        ra = np.rad2deg(phi)
        dec = np.rad2deg(0.5 * np.pi - theta)

        return ra, dec

    def radec2pix(self, ra, dec):
        theta, phi = np.deg2rad(90 - dec), np.deg2rad(ra)
        ipix = hp.ang2pix(self.nside, theta, phi)

        return ipix

    def getMaximumRADec(self, value):
        [ra], [dec] = self.pix2radec(np.where(value == np.max(value))[0])

        return ra, dec

    def getLVCPosition(self):
        res = self.favor2.query("SELECT * FROM scheduler_targets WHERE type='LVC' AND status=get_scheduler_target_status_id('active') ORDER BY time_created DESC", simplify=False)

        # If there are no unobserved targets, we may try to re-observe already completed ones
        # We try to re-observe targets last observed not later than 2 hours ago and created no more than day ago
        # if not res:
        #     res = self.favor2.query("SELECT * FROM scheduler_targets WHERE type='LVC' AND status=get_scheduler_target_status_id('completed') AND (time_completed < favor2_timestamp()-'2 hours'::interval AND time_created > favor2_timestamp()-'1 day'::interval) ORDER BY time_created DESC", simplify=False)

        for r in res:
            base = posixpath.join('TRIGGERS', 'LVC', str(r['external_id']))
            #print base

            mapnames = glob.glob(posixpath.join(base, '*.fits.gz'))
            mapnames.sort()
            mapnames.reverse() # So the lalinference.fits is before bayestar.fits

            for mapname in mapnames:
                # Map scaled for scheduler resolution
                scaledname = posixpath.split(mapname)[-1]
                if '.gz' in scaledname:
                    scaledname = posixpath.splitext(scaledname)[0]
                scaledname = posixpath.join(base, "scaled_" + scaledname)

                #print mapname

                if posixpath.exists(scaledname):
                    skymap, header = hp.read_map(scaledname, h=True, verbose=False)
                else:
                    skymap, header = hp.read_map(mapname, h=True, verbose=False)
                    header = dict(header)
                    skymap = hp.ud_grade(skymap, self.nside, power=-2)
                    hp.write_map(scaledname, skymap)

                # print "start"
                # res2 = self.favor2.query("SELECT * FROM images WHERE type='LVC' and keywords->'TARGET UUID'=%s", (r['uuid'],))
                # print len(res2)
                # for r2 in res2:
                #     dist = ang_sep(r2['ra0'],r2['dec0'],self.ra,self.dec)
                #     skymap[dist<7.5] *= 0.9

                # print "done"

                #print hp.npix2nside(len(skymap)), self.nside

                #print "Absolute:", np.sum(skymap), self.getMaximumRADec(skymap)
                #print "Restricted:", np.sum(skymap*self.restrictions), self.getMaximumRADec(skymap*self.restrictions)

                if np.sum(skymap*self.restrictions_weak) < 0.05:
                    # The target is most probably unobservable
                    continue

                suggestion = {}
                suggestion['id'] = r['id']
                suggestion['uuid'] = r['uuid']
                suggestion['name'] = r['name']
                suggestion['type'] = r['type']
                suggestion['ra'], suggestion['dec'] = self.getMaximumRADec(skymap*self.restrictions_weak)

                # TODO: this should depend on the size of probability region and time since trigger
                suggestion['filter'] = 'Clear'
                suggestion['exposure'] = 60.0
                suggestion['repeat'] = 10

                suggestion['priority'] = r['priority']

                return suggestion

        return None

    def getTargetPosition(self):
        # All targets with fixed positions, highest priority first, newest first
        res = self.favor2.query("SELECT * FROM scheduler_targets WHERE status=get_scheduler_target_status_id('active') AND ra IS NOT NULL AND dec IS NOT NULL ORDER BY priority DESC, time_created DESC", simplify=False)

        if 'targets' not in self.modes:
            return

        for r in res:
            ra, dec = r['ra'], r['dec']
            alt, az = self.get_altaz(ra, dec)
            rvalue = self.restrictions[self.radec2pix(ra, dec)]
            rvalue_weak = self.restrictions_weak[self.radec2pix(ra, dec)]

            #print r, rvalue

            min_r_value = 0.00001

            if r['type'] == 'Swift' or r['type'] == 'Fermi':
                min_r_value = 0.01

            if rvalue_weak > min_r_value:
                suggestion = {}
                suggestion['id'] = r['id']
                suggestion['uuid'] = r['uuid']
                suggestion['name'] = r['name']
                suggestion['type'] = r['type']
                suggestion['ra'] = r['ra']
                suggestion['dec'] = r['dec']
                suggestion['exposure'] = r['exposure']
                suggestion['repeat'] = r['repeat']
                suggestion['filter'] = r['filter']

                suggestion['priority'] = r['priority']

                if r['type'] == 'Swift':
                    #suggestion['filter'] = 'BVR'
                    suggestion['filter'] = 'Multimode' # All other parameters are hardcoded in beholder.lua!
                    suggestion['exposure'] = 5.0
                    suggestion['repeat'] = 120

                if r['type'] == 'Fermi':
                    if r['params'] and r['params'].get('error', 100.0) < 2.0:
                        suggestion['filter'] = 'Multimode' # All other parameters are hardcoded in beholder.lua!
                        suggestion['exposure'] = 5.0
                        suggestion['repeat'] = 120
                    else:
                        suggestion['filter'] = 'Clear'
                        suggestion['exposure'] = 5.0
                        suggestion['repeat'] = 120

                        if rvalue < min_r_value:
                            # For widefield mode, use stronger horizon restrictions
                            continue

                if r['type'] != 'Swift' and r['type'] != 'Fermi':
                    if rvalue < min_r_value:
                        # For normal target imaging mode, use stronger horizon restrictions
                        continue

                return suggestion

        return None

    def getSurveyPosition(self, update=False, use_interests=True, use_coverage=True):
        if update:
            self.update()

        value = np.zeros_like(self.coverage)

        if use_coverage:
            value = np.max(self.coverage) - self.coverage
            if np.max(self.coverage) > np.min(self.coverage):
                value *= 0.5/(np.max(self.coverage) - np.min(self.coverage))

        value += 0.5

        if use_interests:
            value += 0.5*self.iweights

        value *= self.restrictions

        suggestion = {}
        suggestion['type'] = 'monitoring'
        suggestion['name'] = 'monitoring'
        suggestion['filter'] = 'Clear'
        suggestion['exposure'] = self.monitoring_exposure
        suggestion['repeat'] = 1
        suggestion['ra'], suggestion['dec'] = self.getMaximumRADec(value)
        suggestion['priority'] = 1

        return suggestion

    def expireTargets(self):
        res = self.favor2.query("SELECT * FROM scheduler_targets WHERE timetolive > 0 AND favor2_timestamp() - time_created > timetolive::text::interval AND status=get_scheduler_target_status_id('active')", simplify=False)

        for r in res:
            print "Target %d: %s / %s expired" % (r['id'], r['name'], r['type'])
            self.favor2.query("UPDATE scheduler_targets SET status=get_scheduler_target_status_id('failed') WHERE id=%s", (r['id'],))

    def getPointingSuggestion(self):
        self.expireTargets()

        return self.getLVCPosition() or self.getTargetPosition() or self.getSurveyPosition()

    def targetComplete(self, id=0, uuid=None):
        if id:
            self.favor2.query("UPDATE scheduler_targets SET (status, time_completed) = (get_scheduler_target_status_id('completed'),  favor2_timestamp()) WHERE id = %s", (id,))
        elif uuid:
            self.favor2.query("UPDATE scheduler_targets SET (status, time_completed) = (get_scheduler_target_status_id('completed'),  favor2_timestamp()) WHERE uuid = %s", (uuid,))

# Server part

class SchedulerProtocol(Favor2Protocol):
    refresh = 10

    def requestStatus(self):
        pass

    def processMessage(self, string):
        scheduler = self.factory.object
        command = immp.parse(string)

        # exit
        if command.name() == 'exit':
            reactor.stop()

        # get_scheduler_status
        if command.name() == 'get_scheduler_status':
            if (datetime.datetime.utcnow() - scheduler.fast_update_time).total_seconds() > 3.0:
                scheduler.update(fast=True)

            status = 'scheduler_status '
            status += ' zsun=%g' % (90.0-np.rad2deg(scheduler.favor2.sun.alt))
            status += ' zmoon=%g' % (90.0-np.rad2deg(scheduler.favor2.moon.alt))
            status += ' latitude=%g longitude=%g' % (np.rad2deg(scheduler.favor2.obs.lat), np.rad2deg(scheduler.favor2.obs.lon))

            status += ' ninterests=%d' % len(scheduler.interests)
            status += ' modes=%s' % ','.join(scheduler.modes)

            for i,interest in enumerate(scheduler.interests.values()):
                status += ' interest%d_name=%s interest%d_ra=%g interest%d_dec=%g interest%d_radius=%g interest%d_weight=%g' % (i, interest['name'], i, interest['ra'], i, interest['dec'], i, interest['radius'], i, interest['weight'])

            self.message(status)

        # get_pointing_suggestion
        if command.name() == 'get_pointing_suggestion':
            result = 'scheduler_pointing_suggestion'

            scheduler.update()

            suggestion = scheduler.getPointingSuggestion()

            result += ' ' + ' '.join(['%s=%s' % (k, suggestion[k]) for k in suggestion.keys()])

            #print result

            self.message(result)

        # add_interest
        if command.name() == 'add_interest' or command.name() == 'set_interest':
            scheduler.addInterest(command.get('name', 'Unknown'), command.get('ra', 0.0), command.get('dec', 0.0), radius=command.get('radius', command.get('r', 5.0)), weight=command.get('weight', 1.0), reschedule=command.get('reschedule', False), alt=command.get('alt', None), az=command.get('az', None))
            self.message(command.name() + '_done')

        # delete_interest
        if command.name() == 'delete_interest':
            scheduler.deleteInterest(command.get('name', 'Unknown'))
            self.message(command.name() + '_done')

        # target_complete, target_observed
        if command.name() == 'target_complete' or command.name() == 'target_observed':
            scheduler.targetComplete(id=command.get('id', 0), uuid=command.get('uuid', None))
            self.message(command.name() + '_done')

            try:
                reportTargetObserved(id=command.get('id', 0), favor2=scheduler.favor2)
            except:
                import traceback
                traceback.print_exc()
                pass

        # reschedule
        if command.name() == 'reschedule':
            scheduler.update()
            suggestion = scheduler.getPointingSuggestion()
            if suggestion['priority'] > 1.0:
                message = 'reschedule priority=%g' % suggestion['priority']

                if suggestion['type'] == 'Swift' or (suggestion['type'] == 'Fermi' and suggestion['filter'] == 'Pol'):
                    # External trigger with sufficiently accurate coordinates,
                    # suitable for occasional in-FOV follow-up
                    message += ' ra=%g dec=%g id=%d uuid=%s' % (suggestion['ra'], suggestion['dec'], suggestion['id'], suggestion['uuid'])

                # We have high-priority target observable, let's inform everyone!
                self.factory.message(message)

        # add_coverage
        if command.name() == 'add_coverage':
            scheduler.addCoverage(ra=float(command.get('ra0', 0)), dec=float(command.get('dec0', 0)),
                                  sr=0.5*(float(command.get('size_ra', 0))+float(command.get('size_dec', 0))), store=False)
            #scheduler.latest_time = datetime.datetime.utcnow()
            self.message(command.name() + '_done')

        if command.name() == 'add_mode':
            scheduler.modes.add(command.get('mode'))
            self.message(command.name() + '_done')

        if command.name() == 'remove_mode' or command.name() == 'delete_mode':
            if command.get('mode') in scheduler.modes:
                scheduler.modes.remove(command.get('mode'))
            self.message(command.name() + '_done')

def storeSchedulerCoverage(scheduler):
    if scheduler.latest_time > scheduler.store_time:
        scheduler.storeCoverage()

import xml.etree.cElementTree
from ligo.gracedb.rest import GraceDbBasic, HTTPError

def reportTargetObserved(id=0, favor2=None, lvc=False):
    favor2 = favor2 or Favor2()

    target = favor2.query("SELECT * FROM scheduler_targets WHERE id=%s", (id,), simplify=True)

    if target:
        # Send an email
        if target['type'] in ['Swift', 'Fermi', 'LVC']:
            subject = 'scheduler: target %d: %s / %s observed' % (target['id'], target['name'], target['type'])
            body = subject
            body += "\n"
            body += "http://mmt.favor2.info/scheduler/%d" % (target['id'])

            if target['type'] == 'LVC':
                dirname = posixpath.join("TRIGGERS", "LVC", str(target['external_id']))
                imagename = posixpath.join(dirname, "map.png")
                plot_LVC_map(target['id'], file=imagename)
                send_email(body, subject=subject, attachments=[imagename])
            else:
                send_email(body, subject=subject)

        # Submit EM footprint to LVC GraceDB
        if target['type'] == 'LVC' and lvc:
            base = posixpath.join('TRIGGERS', 'LVC', str(target['external_id']))
            files = glob.glob(posixpath.join(base, 'voevent_*.xml'))
            files.sort()

            with open(files[0]) as file:
                # FIXME: all these should be extracted and stored to the DB as soon as trigger arrived!
                root = xml.etree.cElementTree.fromstringlist(file.readlines())
                grace_id = root.find("./What/Param[@name='GraceID']").attrib['value']
                # TODO: make it configurable
                gracedb_username='106083321185023610598@google.com'
                gracedb_password='w588MNtaxcUuJLhnmdSq'
                group = 'GRA SAO RAS'

                raList = []
                decList = []
                timeList = []
                exposureList = []
                raWidthList = []
                decWidthList = []

                images = favor2.query("SELECT * FROM images WHERE keywords->'TARGET ID' = %s::text ORDER BY time", (id,), simplify=False)
                for image in images:
                    raList.append(image['ra0'])
                    decList.append(image['dec0'])
                    timeList.append(image['time'].isoformat())
                    exposureList.append(float(image['keywords']['EXPOSURE']))
                    raWidthList.append(10)
                    decWidthList.append(10)

                #print ra, dec, time, exposure
                client = GraceDbBasic(username=gracedb_username, password=gracedb_password)
                r = client.writeEMObservation(grace_id, group, raList, raWidthList,
                                              decList, decWidthList, timeList, exposureList)

                if r.status == 201:       # 201 means 'Created'
                    print 'Successfully reported the observation to GraceDB'
                else:
                    print 'Error %d reporting the observation to GraceDB' % r.status

if __name__ == '__main__':
    scheduler = Scheduler()
    factory = Favor2ClientFactory(SchedulerProtocol, scheduler)

    from twisted.internet import reactor
    reactor.listenTCP(5557, factory)

    from twisted.internet.task import LoopingCall
    LoopingCall(storeSchedulerCoverage, scheduler).start(120, now=False)

    reactor.run()
