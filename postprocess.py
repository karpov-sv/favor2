#!/usr/bin/env python

import psycopg2, psycopg2.extras
import datetime, re, bisect
import os, sys, time, posixpath, tempfile
import numpy
from astropy import wcs as pywcs
from astropy.io import fits as pyfits
import shutil
import glob

from IPython.core.debugger import Tracer

from favor2 import Favor2, fix_remote_path
from satellites import Satellites
from meteors import Meteor
from survey import find_transients_in_frame, calibrate_objects, get_objects_from_file
import coord
import StringIO

import ephem
from esutil import coords

import numpy as np
import scipy.ndimage as ndimage
import cv2

import secondscale as ss

def get_night(t):
    et = t - datetime.timedelta(hours=12)
    return "%04d_%02d_%02d" % (et.year, et.month, et.day)

def get_filename(time):
    return "%04d%02d%02d%02d%02d%02d%03d.fits" % (time.year, time.month, time.day, time.hour, time.minute, time.second, time.microsecond/1000)

def get_cropped_filename(time):
    return "%04d%02d%02d%02d%02d%02d%03d_crop.fits" % (time.year, time.month, time.day, time.hour, time.minute, time.second, time.microsecond/1000)

def get_time_from_filename(filename):
    m = re.match("^(\d\d\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(\d\d\d)", filename)
    if m:
        t = [int(m.group(i)) for i in range(1,8)]
        return datetime.datetime(t[0], t[1], t[2], t[3], t[4], t[5], t[6]*1000)

def get_time_from_avgname(filename):
    m = re.match("^(\d\d\d\d)(\d\d)(\d\d)_(\d\d)(\d\d)(\d\d)_(\d).", posixpath.split(filename)[-1])
    if m:
        t = [int(m.group(i)) for i in range(1,8)]
        return datetime.datetime(t[0], t[1], t[2], t[3], t[4], t[5]), t[6]

def get_dirname(time):
    return "%04d%02d%02d_%02d%02d%02d" % (time.year, time.month, time.day, time.hour, time.minute, time.second)

def getObjectTrajectory(object, records, order=6):
    time0 = object['time_start']
    Cx = [0 for i in xrange(order)]
    Cy = [0 for i in xrange(order)]

    x = []
    y = []
    t = []

    for record in records:
        x.append(record['x'])
        y.append(record['y'])
        t.append((record['time']-time0).total_seconds())

    if object['state_string'] == 'flash' or object['state_string'] == 'single' or object['state_string'] == 'particle':
        degree = 0
    elif len(records) < 40:
        degree = 1
    elif len(records) < 200:
        degree = 2
    else:
        degree = 3

    cx = numpy.polyfit(t, x, degree)
    cy = numpy.polyfit(t, y, degree)

    for i in xrange(degree + 1):
        Cx[i] = cx[degree - i]
        Cy[i] = cy[degree - i]

    return Cx, Cy

# 2D Gaussian
import scipy.optimize as optimize

def gaussian(height, center_x, center_y, width_x, width_y, rotation, bias):
    """Returns a gaussian function with the given parameters"""
    width_x = float(width_x)
    width_y = float(width_y)

    rotation = np.deg2rad(rotation)
    center_x = center_x * np.cos(rotation) - center_y * np.sin(rotation)
    center_y = center_x * np.sin(rotation) + center_y * np.cos(rotation)

    def rotgauss(x,y):
        xp = x * np.cos(rotation) - y * np.sin(rotation)
        yp = x * np.sin(rotation) + y * np.cos(rotation)
        g = height*np.exp(
            -(((center_x-xp)/width_x)**2+
              ((center_y-yp)/width_y)**2)/2.)
        return g + bias
    return rotgauss

def moments(data):
    """Returns (height, x, y, width_x, width_y)
    the gaussian parameters of a 2D distribution by calculating its
    moments """
    total = data.sum()
    X, Y = np.indices(data.shape)
    x = (X*data).sum()/total
    y = (Y*data).sum()/total
    col = data[:, int(y)]
    width_x = np.sqrt(np.abs((np.arange(col.size)-y)**2*col).sum()/col.sum())
    row = data[int(x), :]
    width_y = np.sqrt(np.abs((np.arange(row.size)-x)**2*row).sum()/row.sum())
    height = data.max()
    return height, x, y, width_x, width_y, 0.1, 0.1

def fitgaussian(data):
    """Returns (height, x, y, width_x, width_y)
    the gaussian parameters of a 2D distribution found by a fit"""
    params = moments(data-np.min(data))
    errorfunction = lambda p: np.ravel(gaussian(*p)(*np.indices(data.shape)) -
                                 data)
    p, cov, _, _, success = optimize.leastsq(errorfunction, params, full_output=True, xtol=1e-15, ftol=1e-15, epsfcn=0.1, maxfev=100000)

    if success not in [1,2,3,4]:
        return None,None

    if cov is None:
        dp = np.zeros_like(p)
    else:
        cov = np.sqrt(cov)*np.std(errorfunction(p))

        dp = cov[0,0],cov[1,1],cov[2,2],cov[3,3],cov[4,4],cov[5,5],cov[6,6]

        if dp[0] > 1e5 or dp[1] > 1e3:
            dp = np.zeros_like(p)

    #print p,dp

    return p,dp

class postprocessor:
    def __init__(self, dbname='favor2', dbhost='', base='.', cutsize=300, channel_id=0):
        if not channel_id:
            # Guess the channel id from hostname
            import socket
            import re

            m = re.match("^mmt(\d+)", socket.gethostname())

            if m:
                id = int(m.group(1))

                if id:
                    print "Channel ID=%d" % id
                    channel_id = id

        if not dbhost and channel_id > 0:
            dbhost = '192.168.16.140'
            print "dbhost=%s" % dbhost

        self.favor2 = Favor2(dbname=dbname, dbhost=dbhost, base=base)
        # self.conn = psycopg2.connect("dbname=" + db)
        # self.conn.autocommit = True
        self.base = base
        self.raw_base = posixpath.join(base, "IMG")
        self.archive_dir = "ARCHIVE"
        self.current_dir = None
        self.cutsize = cutsize
        self.deltat = 1
        self.channel_id = channel_id
        self.width = 2560
        self.height = 2160
        self.clean = False
        self.keep_orig = False
        self.min_length = 60
        self.preview = False

    def find_raw_files(self, time0, time1, channel_id = 0):
        base = self.raw_base

        if channel_id:
            base = posixpath.join('/MMT', '%d' % channel_id, self.raw_base)

        dirs = [d for d in os.listdir(base)
                if os.path.isdir(posixpath.join(base, d)) and re.match('^\d{8}_\d{6}$', d)]
        dirs.sort()

        i0 = max(0, bisect.bisect_right(dirs, get_dirname(time0 - datetime.timedelta(seconds=1))) - 1)
        i1 = bisect.bisect_right(dirs, get_dirname(time1 + datetime.timedelta(seconds=1))) - 1

        #print time0, time1
        name0, name1 = [get_filename(time) for time in [time0, time1]]

        #print name0, name1

        raws = []

        for dir in dirs[i0:i1+1]:
            files = os.listdir(posixpath.join(base, dir))
            files.sort()

            for file in files:
                # Binary search, heh?..
                if file >= name0 and file <= name1:
                    raws.append({'fullname':posixpath.join(base, dir, file), 'filename':file})

        return raws

    def processObjects(self, id=0, reprocess=False, state='all', night=''):
        if type(id) != list and type(id) != tuple:
            id = [id]

        for id1 in id:
            where = [];
            where_opts = [];

            # Properly handle various types of id argument
            if type(id1) == str or type(id1) == unicode:
                id1 = int(id1)

            if not reprocess:
                where.append("NOT EXISTS (SELECT NULL FROM realtime_images i WHERE i.object_id = o.id)")
            if id1:
                where.append("id=%s")
                where_opts.append(int(id1))
            if state and state != 'all':
                where.append("state = get_realtime_object_state_id(%s)")
                where_opts.append(state)
            if night and night != 'all':
                where.append("night = %s")
                where_opts.append(night)
            if self.channel_id:
                where.append("channel_id = %s")
                where_opts.append(self.channel_id)

            if where:
                where_string = "WHERE " + " AND ".join(where)

            res = self.favor2.query("SELECT *, get_realtime_object_state(state) as state_string FROM realtime_objects o %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

            for o in res:
                try:
                    self.favor2.conn.autocommit = False
                    self.processObject(o)
                    self.favor2.conn.commit()
                    self.favor2.conn.autocommit = True
                except:
                    import traceback
                    traceback.print_exc()
                    print "Error processing object %d" % (o['id'])
                    self.favor2.conn.rollback()
                    self.favor2.conn.autocommit = True

    def processObject(self, object, deltat1=0, deltat2=0, cutsize_x=0, cutsize_y=0):
        if not deltat1:
            deltat1 = self.deltat
        if not deltat2:
            deltat2 = self.deltat
        if not cutsize_x:
            cutsize_x = self.cutsize
        if not cutsize_y:
            cutsize_y = self.cutsize

        should_make_animation = True

        if object['state_string'] == 'flash' and deltat1 < 5.0 and deltat2 < 5.0:
            deltat1,deltat2 = 5.0,5.0

        print "Processing object %s: %s at %s, deltat=(%g, %g), cutsize=(%d, %d)" % (object['id'], object['state_string'], object['time_start'], deltat1, deltat2, cutsize_x, cutsize_y)

        path = posixpath.join(self.base, self.archive_dir, object['state_string'], get_night(object['time_start']), str(object['id']))

        # Remove all images associated with this object
        self.favor2.query("DELETE FROM realtime_images WHERE object_id = %s;", (object['id'], ))

        dark = self.favor2.find_image(object['time_start'], type='dark', channel_id=object['channel_id'])
        dark = fix_remote_path(dark, channel_id=object['channel_id'])
        params = object['params']
        time0 = object['time_start']

        # Get files of raw images corresponding to the object time span, extended by given interval
        raws = self.find_raw_files(object['time_start'] - datetime.timedelta(seconds=deltat1), object['time_end'] + datetime.timedelta(seconds=deltat2), channel_id=object['channel_id'])
        # Get all the records of the object
        records = self.favor2.query("SELECT * FROM realtime_records WHERE object_id = %s ORDER BY time;", (object['id'],), simplify=False)

        if len(records) < self.min_length and self.preview and object['state_string'] == 'moving':
            # Faster preview for moving events
            return

        shutil.rmtree(path, ignore_errors = True)

        try:
            os.makedirs(path)
        except:
            pass

        order = 6
        # Cx = [float(params['Cx%d'%d]) for d in xrange(order)]
        # Cy = [float(params['Cy%d'%d]) for d in xrange(order)]

        # Compute trajectory params
        if object['state_string'] != 'meteor':
            Cx, Cy = getObjectTrajectory(object, records, order=order)

        pos = 0
        x0 = records[0]['x']
        y0 = records[0]['y']
        a0 = records[0]['a']

        extent = 5

        # Increase the extent for long or bright events
        if object['state_string'] == 'meteor':
            if len(records) > 10:
                extent = 10
            else:
                for record in records:
                    if record['mag'] > 4:
                        extent = 10
                        break

        # Heuristics to estimate optimal crop size
        x_min, x_max, y_min, y_max = self.width, 0, self.height, 0
        size = 0

        for record in records:
            x_min = min(x_min, record['x'] - max(cutsize_x,
                                                 min(extent*record['a'], min(record['x'], self.width - record['x'] - 1))))
            x_max = max(x_max, record['x'] + max(cutsize_x,
                                                 min(extent*record['a'], min(record['x'], self.width - record['x'] - 1))))
            y_min = min(y_min, record['y'] - max(cutsize_y,
                                                 min(extent*record['a'], min(record['y'], self.height - record['y'] - 1))))
            y_max = max(y_max, record['y'] + max(cutsize_y,
                                                 min(extent*record['a'], min(record['y'], self.height - record['y'] - 1))))
            size = max(cutsize_x, cutsize_y, 3*record['a'])

            #print "x_min=%d x_max=%d y_min=%d y_max=%d" % (x_min, x_max, y_min, y_max)

        Nimages = 0

        for raw in raws:
            time = get_time_from_filename(raw['filename'])
            cropped = posixpath.join(path, get_cropped_filename(time))
            dt = (time - time0).total_seconds()

            if pos < len(records) and get_filename(records[pos]['time']) == raw['filename']:
                record_id = records[pos]['id']
                pos = pos + 1
            else:
                record_id = None

            if object['state_string'] == 'meteor' or object['state_string'] == 'misc':
                x_min, x_max = max(0, x_min), min(self.width - 1, x_max)
                y_min, y_max = max(0, y_min), min(self.height - 1, y_max)

                x0, y0 = 0.5*(x_min + x_max), 0.5*(y_min + y_max)
                size_x, size_y = (x_max - x_min), (y_max - y_min)

                size_x = min(size_x, 2.0*max(x0, self.width - x0 - 1))
                size_y = min(size_y, 2.0*max(y0, self.height - y0 - 1))
            else:
                x0 = round(sum([Cx[n]*dt**n for n in xrange(order)]))
                y0 = round(sum([Cy[n]*dt**n for n in xrange(order)]))
                size_x, size_y = size, size

            command = "./crop x0=%d y0=%d width=%d height=%d %s %s" % (int(x0), int(y0), int(size_x), int(size_y), raw['fullname'], cropped)

            if dark and not self.preview:
                command = command + " dark=%s" % (dark)

            if self.clean or (object['state_string'] == 'meteor' and not self.preview):
                command = command + " -clean"

            if not self.preview or record_id:
                #print command
                os.system(command)

                self.favor2.query("INSERT INTO realtime_images (object_id, record_id, filename, time) VALUES (%s, %s, %s, %s);", (object['id'], record_id, cropped, time))

                if self.keep_orig: # or object['state_string'] == 'meteor':
                    # Copy original images along with cropped ones
                    shutil.copy(raw['fullname'], path)

                Nimages += 1

            if self.preview and pos >= 3:
                # Three images with detected object is enoungh for preview
                break

        if object['state_string'] == 'meteor':
            m = self.analyzeMeteor(object)
            if m and len(m.time) and not m.laser:
                should_reprocess = False

                #print "x0=%d y0=%d size_x=%d size_y=%d" % (x0, y0, size_x, size_y)

                if (object['time_start'] - m.time[0]).total_seconds() > deltat1 - 0.5 and deltat1 < 3.0:
                    deltat1 = deltat1 + 1.0
                    should_reprocess = True

                if (m.time[-1] - object['time_end']).total_seconds() > deltat2 - 0.5 and deltat2 < 3.0:
                    deltat2 = deltat2 + 1.0
                    should_reprocess = True

                # TODO: add spatial extent if necessary!
                if (min(m.x_start + m.x_end) < 0.5*cutsize_x and x0 - 0.5*size_x > 2) or (max(m.x_start + m.x_end) > m.shape[1] - 0.5*cutsize_x and x0 + 0.5*size_x + 1 < self.width):
                    cutsize_x *= 2
                    should_reprocess = True

                if (min(m.y_start + m.y_end) < 0.5*cutsize_y and y0 - 0.5*size_y > 2) or (max(m.y_start + m.y_end) > m.shape[0] - 0.5*cutsize_y and y0 + 0.5*size_y + 1 < self.height):
                    cutsize_y *= 2
                    should_reprocess = True

                if should_reprocess:
                    print "Reprocessing with extended time or spatial range"
                    self.processObject(object, deltat1=deltat1, deltat2=deltat2, cutsize_x=cutsize_x, cutsize_y=cutsize_y)
                    should_make_animation = False
                else:
                    # We already reprocessed the object or it should not be reprocessed at all
                    # if m.filter == 'R':
                    #    self.processMulticolorMeteor(object)
                    pass

        if Nimages and should_make_animation:
            # Imagemagick seems to support uncompressed files only
            print "Producing an animation"
            os.system("convert -flip -resize '800x800>' -contrast-stretch 1%%x0.5%% %s/*_crop.fits %s/animation.gif" % (path, path))

            # Now compress all FITS files
            files = [posixpath.join(path, f) for f in os.listdir(path) if re.match('^.*\.fits$', f)]
            for file in files:
                os.system('./crop -compress %s %s' % (file, file))

            print "%d images in %s" % (Nimages, path)

    def processMulticolorMeteor(self, object, extsize=400, nchannels=9, reprocess=True):
        path = posixpath.join(self.base, self.archive_dir, object['state_string'], get_night(object['time_start']), str(object['id']))

        if not reprocess and (posixpath.exists(posixpath.join(path, 'channels')) or posixpath.exists(posixpath.join(path, 'analysis', 'multicolor.txt'))):
            return

        m = Meteor(path, load=True)

        files=glob.glob(posixpath.join(path, '*_crop.fits'))
        files.sort()
        times = [get_time_from_filename(posixpath.split(file)[-1]) for file in files]

        deltat = datetime.timedelta(seconds=0.05)

        for cid in xrange(1, nchannels):
            if cid == object['channel_id']:
                continue

            raws = self.find_raw_files(times[0]-deltat, times[-1]+deltat, channel_id=cid)

            if not raws:
                continue

            header = pyfits.getheader(raws[0]['fullname'], -1)
            filter = header['FILTER']
            width, height = header['NAXIS1'], header['NAXIS2']
            wcs = pywcs.WCS(header=header)
            [x1, x2], [y1, y2] = wcs.all_world2pix(m.ra_start[[0,-1]], m.dec_start[[0,-1]], 0)

            [x1,x2] = [min([x1,x2]), max([x1,x2])]
            [y1,y2] = [min([y1,y2]), max([y1,y2])]

            if ((x1 < width and y1 < height and x2 >= 0 and y2 >= 0)):
                x1 -= extsize
                y1 -= extsize
                x2 += extsize
                y2 += extsize

                x0, y0 = 0.5*(x1 + x2), 0.5*(y1 + y2)
                size_x, size_y = x2 - x1, y2 - y1

                print cid, x1, y1, x2, y2

                cpath = posixpath.join(path, 'channels', str(cid) + '_' + filter)

                dark = self.favor2.find_image(m.time[0], type='dark', channel_id=cid)
                dark = fix_remote_path(dark, channel_id=cid)

                try:
                    os.makedirs(cpath)
                except:
                    pass

                for raw in raws:
                    time = get_time_from_filename(raw['filename'])
                    cropped = posixpath.join(cpath, get_cropped_filename(time))

                    #print raw['filename'], cropped

                    command = "./crop x0=%d y0=%d width=%d height=%d %s %s -clean" % (int(x0), int(y0), int(size_x), int(size_y), raw['fullname'], cropped)

                    if dark:
                        command = command + " dark=%s" % (dark)

                    os.system(command)

                if raws:
                    # Imagemagick seems to support uncompressed files only
                    os.system("convert -flip -resize '800x800>' -contrast-stretch 1%%x0.5%% %s/*_crop.fits %s/animation.gif" % (cpath, cpath))

                    # Now compress all FITS files
                    files = [posixpath.join(cpath, f) for f in os.listdir(cpath) if re.match('^.*\.fits$', f)]
                    for file in files:
                        os.system('./crop -compress %s %s' % (file, file))

        m.analyzeChannels()

    def processMulticolorMeteors(self, night='all', id=0, reprocess=False):
        where = ["state = get_realtime_object_state_id('meteor')"]
        where_opts = []

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        if id:
            where.append("id = %s")
            where_opts.append(id)

        where.append("(params->'analyzed') = '1'")

        where.append("(params->'filter')='R'")

        if self.channel_id:
            where.append("channel_id = %s")
            where_opts.append(self.channel_id)

        where_string = " AND ".join(where)
        if where_string:
            where_string = " WHERE " + where_string

        res = self.favor2.query("SELECT *, get_realtime_object_state(state) as state_string FROM realtime_objects %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

        N = 0

        for obj in res:
            print "%d / %d - %d" % (N, len(res), obj['id'])
            self.processMulticolorMeteor(obj, extsize=300, reprocess=reprocess)
            N = N + 1

    def coaddPointings(self, night='all', all=False):
        if night == 'all':
            night = None

        pointings = self.favor2.find_pointings(night=night)

        for p in pointings:
            if p['length'] < 2:
                continue

            filename = p['first'].replace('avg', 'coadd')
            if self.favor2.query('SELECT id FROM images WHERE filename=%s', (filename,)) and not all:
                print "%s already in archive, skippng" % (filename)
                continue

            # filename is relative to self.favor2.base, so ...
            self.favor2.coadd_archive_images(p['filenames'], posixpath.join(self.favor2.base, filename))
            # register_image accepts filenames relative to base, no need to change it
            self.favor2.register_image(filename, 'coadd')
            print filename

    def mergeMeteors(self, night='all'):
        where = [];
        where_opts = [];

        if night and night != 'all':
            where.append("o1.night = %s")
            where.append("o1.night = o2.night")
            where_opts.append(night)

        if where:
            where_string = "AND " + " AND ".join(where)
        else:
            where_string = ""

        res = self.favor2.query("SELECT o1.id AS id1, o2.id AS id2 FROM realtime_objects o1, realtime_objects o2 WHERE o1.state = get_realtime_object_state_id('meteor') AND o2.state = get_realtime_object_state_id('meteor') AND o1.id < o2.id AND o1.channel_id = o2.channel_id AND tsrange(o1.time_start-'0.5s'::interval, o1.time_end+'0.5s'::interval) && tsrange(o2.time_start-'0.5s'::interval, o2.time_end+'0.5s'::interval) %s;" % (where_string), where_opts, simplify=False)

        groups = []

        for r in res:
            known = False
            v1, v2 = r['id1'], r['id2']

            for g in groups:
                if v1 in g or v2 in g:
                    g.add(v1)
                    g.add(v2)
                    known = True
                    break

            if not known:
                groups.append(set([v1, v2]))

        for i in xrange(len(groups)):
            for g1 in groups:
                for g2 in groups:
                    if g1 != g2 and not g1.isdisjoint(g2):
                        groups.append(g1.union(g2))
                        groups.remove(g1)
                        groups.remove(g2)
                        break

        self.favor2.conn.autocommit = False

        for g in groups:
            id1 = g.pop()
            for id2 in g:
                print "Merging object %d into %d" % (id2, id1)
                self.favor2.query("UPDATE realtime_records SET object_id = %s WHERE object_id = %s;", (id1, id2))
                self.favor2.delete_object(id2)
                self.favor2.query("UPDATE realtime_objects o SET time_end = (SELECT max(time) FROM realtime_records WHERE object_id=o.id) WHERE id=%s;", (id1,))
                self.favor2.query("UPDATE realtime_objects o SET time_start = (SELECT min(time) FROM realtime_records WHERE object_id=o.id) WHERE id=%s;", (id1,))

        # Now let's mark simultaneous meteors from different channels
        res = self.favor2.query("SELECT o1.id AS id1, o2.id AS id2 FROM realtime_objects o1, realtime_objects o2 WHERE o1.state = get_realtime_object_state_id('meteor') AND o2.state = get_realtime_object_state_id('meteor') AND o1.id < o2.id AND o1.channel_id != o2.channel_id AND tsrange(o1.time_start-'0.1s'::interval, o1.time_end+'0.5s'::interval) && tsrange(o2.time_start-'0.5s'::interval, o2.time_end+'0.1s'::interval) %s;" % (where_string), where_opts, simplify=False)

        where = [];
        where_opts = [];

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        if where:
            where_string = "AND " + " AND ".join(where)
        else:
            where_string = ""

        self.favor2.query("UPDATE realtime_objects SET params = params - 'related'::text - 'multi'::text WHERE (params->'multi') IS NOT NULL AND state = get_realtime_object_state_id('meteor') %s;" % (where_string), where_opts)

        groups = []

        for r in res:
            known = False
            v1, v2 = r['id1'], r['id2']

            for g in groups:
                if v1 in g or v2 in g:
                    g.add(v1)
                    g.add(v2)
                    known = True
                    break

            if not known:
                groups.append(set([v1, v2]))

        for i in xrange(len(groups)):
            for g1 in groups:
                for g2 in groups:
                    if g1 != g2 and not g1.isdisjoint(g2):
                        groups.append(g1.union(g2))
                        groups.remove(g1)
                        groups.remove(g2)
                        break

        for g in groups:
            multi = " ".join([str(s) for s in g])
            #print "Multi-channel meteor: %s" % " ".join([str(s) for s in g])

            for id in g:
                related = " ".join([str(s) for s in g if s != id])
                self.favor2.query("UPDATE realtime_objects SET params = params || '\"related\"=>\""+related+"\"'::hstore WHERE id = %s;", (id,))
                self.favor2.query("UPDATE realtime_objects SET params = params || '\"multi\"=>\""+multi+"\"'::hstore WHERE id = %s;", (id,))

        self.favor2.conn.commit()
        self.favor2.conn.autocommit = True

    def filterLaserEvents(self, night='all', state='meteor'):
        where = ['state = get_realtime_object_state_id(%s)']
        where_opts = [state]

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        where_string = " AND ".join(where)
        if where_string:
            where_string = "WHERE " + where_string

        where_opts.append(self.min_length)

        res = self.favor2.query("SELECT id, params, nrecords FROM (SELECT id, params, (SELECT count(*) FROM realtime_records WHERE object_id=realtime_objects.id) nrecords FROM realtime_objects " + where_string + ") t WHERE nrecords>%s OR (params->'laser') = '2';", where_opts, simplify=False)

        self.favor2.conn.autocommit = False
        for r in res:
            print "Deleting object %s with %s records" % (r['id'], r['nrecords'])
            # Filter satellites detected by the same channel at the same moment
            # sats = self.favor2.query("SELECT o.* FROM realtime_objects o, realtime_objects o0 WHERE o0.id = %s AND o0.id != o.id AND o0.channel_id=o.channel_id AND o.time_start < o0.time_end AND o.time_end > o0.time_start", (r['id'],), simplify=False)
            # for sat in sats:
            #     print " Deleting overlapping object %s" % (sat['id'])
                #self.favor2.query("DELETE FROM realtime_objects WHERE id = %s;", (sat['id'],))

            self.favor2.delete_object(r['id'])

        self.favor2.conn.commit()
        self.favor2.conn.autocommit = True

    def filterSatelliteMeteors(self, night='all', state='meteor'):
        where = ['state = get_realtime_object_state_id(%s)']
        where_opts = [state]

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        where_string = " AND ".join(where)
        if where_string:
            where_string = "WHERE " + where_string

        res = self.favor2.query("SELECT * FROM realtime_objects %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

        prev_night = ''
        sats = Satellites()

        for obj in res:
            #print obj['id'], obj['time_start']
            if prev_night != obj['night']:
                sats.readTles(time=obj['time_start'])
                prev_night = obj['night']

            records = self.favor2.query("SELECT * FROM realtime_records WHERE object_id = %s ORDER BY time;", (obj['id'],), simplify=False)
            Ntotal = 0
            Nmatched = 0
            names = set()

            for r in records:
                s = sats.getSatellite(ra=r['ra'], dec=r['dec'], time=r['time'], sr=0.3)
                Ntotal = Ntotal + 1

                if s:
                    names.add(s.name)
                    Nmatched = Nmatched + 1

            if Nmatched:
                print "%d: %d of %d matched, %s" % (obj['id'], Nmatched, Ntotal, names)

    def analyzeMeteor(self, obj):
        path = posixpath.join(self.base, self.archive_dir, obj['state_string'], obj['night'], str(obj['id']))

        try:
            m = Meteor(path, favor2=self.favor2)
            if m.analyzed:
                self.favor2.query("UPDATE realtime_objects SET params = params || 'analyzed=>1, ra_start=>%s, dec_start=>%s, ra_end=>%s, dec_end=>%s, nframes=>%s, ang_vel=>%s, z_start=>%s, z_end=>%s, mag_calibrated=>%s, mag0=>%s, mag_min=>%s, mag_max=>%s, filter=>" + m.filter + "'::hstore WHERE id = %s;", (m.ra_start[m.i_start], m.dec_start[m.i_start], m.ra_end[m.i_end], m.dec_end[m.i_end], m.nframes, m.ang_vel, m.z_start, m.z_end, m.mag_calibrated, m.mag0, m.mag_min, m.mag_max, obj['id']))
            else:
                self.favor2.query("UPDATE realtime_objects SET params = params || 'analyzed=>0'::hstore WHERE id = %s;", (obj['id'],))
            if m.laser:
                self.favor2.query("UPDATE realtime_objects SET params = params || 'laser=>1'::hstore WHERE id = %s;", (obj['id'],))
                return None

            return m
        except:
            import traceback
            traceback.print_exc()

            self.favor2.query("UPDATE realtime_objects SET params = params || 'analyzed=>0'::hstore WHERE id = %s;", (obj['id'],))

            print "Error analyzing meteor %d" % obj['id']

            return None

    def analyzeMeteors(self, night='all', id=0, reprocess=False):
        where = ["state = get_realtime_object_state_id('meteor')"]
        where_opts = []

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        if id:
            where.append("id = %s")
            where_opts.append(id)

        if not reprocess:
            where.append("(params->'analyzed') IS NULL")

        if self.channel_id:
            where.append("channel_id = %s")
            where_opts.append(self.channel_id)

        where_string = " AND ".join(where)
        if where_string:
            where_string = " WHERE " + where_string

        res = self.favor2.query("SELECT *, get_realtime_object_state(state) as state_string FROM realtime_objects %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

        N = 0

        for obj in res:
            self.analyzeMeteor(obj)
            N = N + 1
            print "%d / %d" % (N, len(res))

    def filterFlashesByClassification(self, night='all'):
        where = ["state = get_realtime_object_state_id('flash')"]
        where_opts = []

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        where.append("params->'classification' LIKE '%% star %%'")

        where_string = " AND ".join(where)
        if where_string:
            where_string = " WHERE " + where_string

        res = self.favor2.query("DELETE FROM realtime_objects %s;" % (where_string), where_opts, simplify=False)

    def filterFlashes(self, id=0, night='all', state='flash'):
        where = ["state = get_realtime_object_state_id(%s)"]
        where_opts = [state]

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        if self.channel_id:
            where.append("channel_id = %s")
            where_opts.append(self.channel_id)

        if id:
            where.append("id = %s")
            where_opts.append(id)

        where_string = " AND ".join(where)
        if where_string:
            where_string = " WHERE " + where_string

        # self.filterFlashesByClassification(night=night)

        res = self.favor2.query("SELECT *, get_realtime_object_state(state) as state_string FROM realtime_objects %s ORDER BY time_start DESC;" % (where_string), where_opts, simplify=False)

        sats = None

        for obj in res:
            #if 'satellite' in obj['params']['classification']:
            #    continue

            raws = None
            records = self.favor2.query("SELECT * FROM realtime_records WHERE object_id = %s ORDER BY time;", (obj['id'],), simplify=False)
            is_delete = False

            mags = [r['mag'] for r in records if r['mag'] < 20]
            if len(mags) and mags[0] != 0:
                min_mag = min(mags)
            else:
                is_delete = True

            ra, dec, time0 = None, None, None

            pixscale = 16.0/3600

            if records and not is_delete:
                Cx, Cy = getObjectTrajectory(obj, records)
                x0, y0 = Cx[0], Cy[0]

                ra, dec, time0 = records[0]['ra'], records[0]['dec'], records[0]['time']

            if records and not is_delete and True:
                # Check whether the record is on the frame edge
                for record in records:
                    if record['x'] < 1 or record['y'] < 1 or record['x'] > 2558 or record['y'] > 2158:
                        is_delete = True
                        break

            if records and not is_delete and True:
                # Check for Tycho2 stars using RA/Dec computed in real time for first record
                ra, dec, time0 = records[0]['ra'], records[0]['dec'], records[0]['time']

                stars = self.favor2.query("SELECT ra, dec, 0.76*bt+0.24*vt as b , 1.09*vt-0.09*bt as v, 0 as r FROM tycho2 WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY vt ASC LIMIT %s;", (ra, dec, 5*pixscale, 1000, ), simplify=False)

                for star in stars:
                    # TODO: use more correct estimation of possible scintillation amplitude
                    if star['v'] < 11.0: # min_mag + 5:
                        print "Object %d : Tycho2 star at %g\", %g mag (using realtime coordinates)" % (obj['id'], float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600, star['v'])
                        is_delete = True

                # And now 2MASS
                stars = self.favor2.query("SELECT ra, dec, h FROM twomass WHERE q3c_radial_query(ra, dec, %s, %s, %s) AND h IS NOT NULL ORDER BY h ASC LIMIT %s;", (ra, dec, 5*pixscale, 1000, ), simplify=False)

                for star in stars:
                    # TODO: use more correct estimation of possible scintillation amplitude
                    if star['h'] < 10.0: # min_mag + 5:
                        print "Object %d : 2MASS star at %g\", %g mag (using realtime coordinates)" % (obj['id'], float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600, star['h'])
                        is_delete = True

                stars = self.favor2.query("SELECT ra, dec, h FROM twomass WHERE q3c_radial_query(ra, dec, %s, %s, %s) AND h IS NOT NULL ORDER BY h ASC LIMIT %s;", (ra, dec, 10*pixscale, 1000, ), simplify=False)

                for star in stars:
                    # TODO: use more correct estimation of possible scintillation amplitude
                    if star['h'] < 6.0: # min_mag + 5:
                        print "Object %d : 2MASS star at %g\", %g mag (using realtime coordinates)" % (obj['id'], float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600, star['h'])
                        is_delete = True

                if not is_delete:
                    #print "No stars at realtime coords", ra, dec, "for object", obj['id']
                    pass

            if ra is not None and True:
                # Check for planets
                self.favor2.obs.date = obj['time_start']

                for o in [ephem.Venus(), ephem.Mars(), ephem.Jupiter(), ephem.Saturn(), ephem.Uranus(), ephem.Neptune()]:
                    sr = 0.5
                    o.compute(self.favor2.obs)
                    if coords.sphdist(np.rad2deg(o.ra), np.rad2deg(o.dec), ra, dec)[0] < sr:
                        print "Object %d is a planet" % obj['id']
                        is_delete = True

            if not is_delete and True:
                # Jumpy tracking
                avg1 = self.favor2.query('SELECT * FROM images WHERE channel_id=%s AND time<%s AND type=%s ORDER BY time DESC LIMIT 1 OFFSET 1', (obj['channel_id'], obj['time_start'], 'avg'), simplify=True)
                avg2 = self.favor2.query('SELECT * FROM images WHERE channel_id=%s AND time>%s AND type=%s ORDER BY time LIMIT 1 OFFSET 1', (obj['channel_id'], obj['time_start'], 'avg'), simplify=True)

                filename1 = fix_remote_path(avg1['filename'], channel_id=obj['channel_id'])
                filename2 = fix_remote_path(avg2['filename'], channel_id=obj['channel_id'])

                if filename1 and filename2 and posixpath.exists(filename1) and posixpath.exists(filename2):
                    header1 = pyfits.getheader(filename1, -1)
                    header2 = pyfits.getheader(filename2, -1)

                    wcs1 = pywcs.WCS(header1)
                    wcs2 = pywcs.WCS(header2)

                    [ra1], [dec1] = wcs1.all_pix2world([x0], [y0], 0)
                    [ra2], [dec2] = wcs2.all_pix2world([x0], [y0], 0)

                    if (avg2['time'] - avg1['time']).total_seconds() < 50.0:
                        #print avg1['id'], avg2['id'], (avg2['time'] - avg1['time']).total_seconds()

                        if coords.sphdist(ra1, dec1, ra2, dec2)[0]*3600 > 40.0:
                            print obj['id'], ':', avg1['id'], avg2['id'], (avg2['time'] - avg1['time']).total_seconds(), '-', coords.sphdist(ra1, dec1, ra2, dec2)[0]*3600
                            is_delete = True

            if records and not is_delete and False:
                # Check the flash PSF
                fluxes = [_['flux'] for _ in records]
                imax=np.where(fluxes==np.max(fluxes))[0][0]
                x1,y1 = records[imax]['x'],records[imax]['y']

                img = None
                header = None

                for filename in self.favor2.query('SELECT filename FROM realtime_images WHERE object_id=%s AND record_id IS NOT NULL', (obj['id'],), simplify=False):
                    #print filename
                    filename = fix_remote_path(filename[0], channel_id=obj['channel_id'])
                    if filename and posixpath.exists(filename):
                        img1 = pyfits.getdata(filename, -1)
                        header1 = pyfits.getheader(filename, -1)

                        if header is None:
                            header = header1
                            x1 -= header['CROP_X0']
                            y1 -= header['CROP_Y0']

                        if img is None:
                            img = 1.0*img1
                        else:
                            img += img1

                if img is not None:
                    if x1 < 0 or y1 < 0 or x1 >= img.shape[1] or y1 >= img.shape[0]:
                        print "Coordinates outside sub-frame"
                        is_delete = True
                        #pass
                    else:
                        x11,y11 = int(np.round(x1)),int(np.round(y1))
                        p,dp = fitgaussian(img[y11-10:y11+10,x11-10:x11+10])
                        print p
                        print dp
                        #if p is None or 3.0*dp[0] > p[0] or p[1] < 0 or p[1] > 20 or p[2] < 0 or p[2] > 20 or p[3] > 10 or p[4] > 10 or dp[1] > 10 or dp[2] > 10:
                        if p is None or 2.0*dp[0] > p[0]:
                            #print p,dp
                            print "No good PSF for object", obj['id']
                            is_delete = True
                            #continue
                        else:
                            #continue
                            pass

            # if not is_delete:
            #     continue

            if records and not is_delete and True:
                # Check for Tycho2 stars using blind match of first cropped frame containing the event
                filename = self.favor2.query('SELECT filename FROM realtime_images WHERE object_id=%s AND record_id IS NOT NULL ORDER BY record_id LIMIT 1', (obj['id'],), simplify=True)
                if filename:
                    filename = fix_remote_path(filename, channel_id=obj['channel_id'])
                if filename and posixpath.exists(filename):
                    wcs = None # self.favor2.blind_match_file(filename)

                    # Laser?..
                    img = pyfits.getdata(filename, -1)
                    mask = ndimage.gaussian_filter(img, 1)
                    thresh = np.percentile(mask, 95)
                    _,mask = cv2.threshold(mask.astype(np.float32), thresh, 1, cv2.THRESH_BINARY)
                    lines = cv2.HoughLines(mask.astype(np.uint8), 1, np.pi*1/180, int(0.5*img.shape[1]))

                    if lines is not None and lines.shape[1]:
                        rho, theta = lines[0][0]
                        x = 0.5*img.shape[1]
                        y = 0.5*img.shape[0]
                        a = y*np.cos(theta) - x*np.sin(theta)
                        b = rho - x*np.cos(theta) - y*np.sin(theta)

                        if abs(b) > 20:
                            print obj['id'], "line found but a=%g b=%g - not a laser" % (a, b)
                        else:
                            print obj['id'], "probably laser event"
                            is_delete = True

                    if not wcs and not is_delete:
                        avg = self.favor2.query('SELECT * FROM images WHERE channel_id=%s AND time<%s AND type=%s ORDER BY time DESC LIMIT 1', (obj['channel_id'], obj['time_start'], 'avg'), simplify=True)

                        if (avg['time'] - obj['time_start']).total_seconds() < 30:
                            filename = avg['filename']
                            dark = self.favor2.find_image(obj['time_start'], type='dark', channel_id=obj['channel_id'])
                            dark = fix_remote_path(dark, channel_id=obj['channel_id'])
                            if filename:
                                filename = fix_remote_path(filename, channel_id=obj['channel_id'])
                            if filename and posixpath.exists(filename):
                                wcs = self.favor2.blind_match_file(filename, darkname=dark)
                        else:
                            is_delete = True

                    if wcs:
                        header = pyfits.getheader(filename, -1)
                        if header.get('CROP_X0') is not None:
                            cx0 = x0 - header['CROP_X0']
                            cy0 = y0 - header['CROP_Y0']
                        elif '_crop' in filename:
                            cx0 = 0.5*header['NAXIS1']
                            cx0 = 0.5*header['NAXIS2']
                        else:
                            cx0,cy0 = x0,y0
                        [ra], [dec] = wcs.all_pix2world([cx0], [cy0], 0)

                        stars = self.favor2.query("SELECT ra, dec, 0.76*bt+0.24*vt as b , 1.09*vt-0.09*bt as v, 0 as r FROM tycho2 WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY vt ASC LIMIT %s;", (ra, dec, 5*pixscale, 1000, ), simplify=False)

                        for star in stars:
                            # print star['v'], min_mag, float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600
                            # TODO: use more correct estimation of possible scintillation amplitude
                            if star['v'] < 11.0: # min_mag + 1:
                                print "Object %d : star at %g\", %g mag (using blind-matched coordinates)" % (obj['id'], float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600, star['v'])
                                is_delete = True
                        if not is_delete:
                            #print "No stars at ",cx0, cy0, " - ", ra, dec, "for object", obj['id']
                            pass
                    else:
                        print "No WCS matched for object %d" % obj['id']
                        #is_delete = True
                else:
                    is_delete = True

            if not is_delete and True:
                # Check whether we (still) have RAW files for the event, and delete if not
                raws = self.find_raw_files(obj['time_start'] - datetime.timedelta(seconds=self.deltat), obj['time_end'] + datetime.timedelta(seconds=self.deltat), channel_id=obj['channel_id'])
                have_raws = False

                for raw in raws:
                    if get_filename(records[0]['time']) == raw['filename']:
                        have_raws = True
                        break

                if not have_raws:
                    print "Object %d : no RAW files" % obj['id']
                    #is_delete = True

            if not is_delete and raws and True:
                # Check for Tycho2 stars using blind match of first frame containing the event
                for raw in raws:
                    if get_filename(records[0]['time']) == raw['filename']:
                        wcs = self.favor2.blind_match_file(raw['fullname'])

                        if wcs:
                            [ra], [dec] = wcs.all_pix2world([x0], [y0], 0)

                            stars = self.favor2.query("SELECT ra, dec, 0.76*bt+0.24*vt as b , 1.09*vt-0.09*bt as v, 0 as r FROM tycho2 WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY vt ASC LIMIT %s;", (ra, dec, 5*pixscale, 1000, ), simplify=False)

                            for star in stars:
                                # print star['v'], min_mag, float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600
                                # TODO: use more correct estimation of possible scintillation amplitude
                                if star['v'] < min_mag + 2:
                                    print "Object %d : star at %g\", %g mag (using blind-matched coordinates)" % (obj['id'], float(coord.ang_sep(ra, dec, star['ra'], star['dec']))*3600, star['v'])
                                    is_delete = True
                        else:
                            # Can't match the image - therefore, it is noise event or so
                            print "Object %d : can't match the coordinates" % (obj['id'])
                            is_delete = True

                        break

            if not is_delete and True and raws and len(raws) and os.path.isfile(raws[0]['fullname']) and os.path.isfile(raws[-1]['fullname']):
                # Check for image jumps due to bad tracking
                wcs1 = self.favor2.blind_match_file(raws[0]['fullname'])
                wcs2 = self.favor2.blind_match_file(raws[-1]['fullname'])

                if wcs1 and wcs2:
                    [ra1], [dec1] = wcs1.all_pix2world([x0], [y0], 0)
                    [ra2], [dec2] = wcs2.all_pix2world([x0], [y0], 0)
                    sr = float(coord.ang_sep(ra1, dec1, ra2, dec2))

                    if sr > 1.0*pixscale:
                        print "Object %d / channel %d, shift %g\"" % (obj['id'], obj['channel_id'], sr*3600)
                        is_delete = True
                else:
                    # Can't match images - therefore, it is noise event or so
                    print "Object %d : can't match the coordinates" % (obj['id'])
                    is_delete = True

            if not is_delete and True:
                # Laser?..
                time0, x0, y0, size_x, size_y = records[0]['time'], records[0]['x'], records[0]['y'], 300, 300
                dark = self.favor2.find_image(time0, type='dark', channel_id=obj['channel_id'])
                raws = self.find_raw_files(time0, time0, channel_id=obj['channel_id'])

                if raws:
                    fitsname = raws[0]['fullname']
                    fd, tmpname = tempfile.mkstemp(suffix='.fits')
                    os.close(fd)

                    try:
                        command = "./crop x0=%d y0=%d width=%d height=%d %s %s" % (round(x0), round(y0), round(size_x), round(size_y), fitsname, tmpname)
                        if dark:
                            command = command + " dark=%s" % (dark)

                        os.system(command)

                        img = pyfits.getdata(tmpname)

                        mask = ndimage.gaussian_filter(img, 1)
                        thresh = np.percentile(mask, 95)
                        _,mask = cv2.threshold(mask.astype(np.float32), thresh, 1, cv2.THRESH_BINARY)
                        lines = cv2.HoughLines(mask.astype(np.uint8), 1, np.pi*1/180, int(0.5*size_x))

                        if lines is not None and lines.shape[1]:
                            rho, theta = lines[0][0]
                            x = 0.5*size_x
                            y = 0.5*size_y
                            a = y*np.cos(theta) - x*np.sin(theta)
                            b = rho - x*np.cos(theta) - y*np.sin(theta)

                            if abs(b) > 20:
                                print obj['id'], "line found but a=%g b=%g - not a laser" % (a, b)
                            else:
                                print obj['id'], "probably laser event"
                                is_delete = True
                        else:
                            # pyfits.writeto('out_image.fits', img, clobber=True)
                            # pyfits.writeto('out_mask.fits', mask.astype(np.uint8), clobber=True)
                            pass
                    except:
                        import traceback
                        traceback.print_exc()
                        pass

                    os.unlink(tmpname)

            if not is_delete and ra and dec and time0 and 'satellite' not in obj['params']['classification'] and True:
                # Check for satellites at this point
                if not sats:
                    sats = Satellites()

                classification = None

                for sat in sats.getSatellites(ra, dec, time0, 1):
                    dist = 3600*float(coord.ang_sep(ra, dec, sat.ra*180.0/numpy.pi, sat.dec*180.0/numpy.pi))

                    print "Object %d : satellite %s at %g\"" % (obj['id'], sat.name, dist)
                    classification = "\n".join(filter(None, (classification, "%s satellite at %g arcsec" % (sat.name, dist))))

                if classification:
                    old_classification = self.favor2.query("SELECT params->'classification' FROM realtime_objects WHERE id=%s;", (obj['id'],))
                    if old_classification and " satellite " not in old_classification:
                        classification = "\n".join(filter(None, (classification, old_classification)))

                    self.favor2.query("UPDATE realtime_objects SET params = params || 'classification=>\""+classification+"\"'::hstore WHERE id=%s", (obj['id'],))

            if is_delete:
                print "Deleting object ",obj['id']
                #self.favor2.delete_object(obj['id'])

    def filterSatellites(self, night='all', state='moving', min_length=100):
        where = ['o.state = get_realtime_object_state_id(%s)']
        where_opts = [state]

        where.append("ot.count > %s")
        where_opts.append(min_length)

        if night and night != 'all':
            where.append("o.night = %s")
            where_opts.append(night)

        where_string = " AND ".join(where)
        if where_string:
            where_string = "AND " + where_string

        res = self.favor2.query("WITH t AS (SELECT r.object_id AS id, count(r.id) AS count FROM realtime_records r GROUP BY r.object_id) SELECT ot.count, o.*, get_realtime_object_state(o.state) AS state_string FROM realtime_objects o, t ot WHERE ot.id=o.id %s ORDER BY o.id;" % (where_string), where_opts, simplify=False, debug=True)

        for object in res:
            # if object['id'] < 1544757 or object['id'] > 1545892:
            #     continue
            # if object['id'] != 1642457:
            #     continue

            records = self.favor2.query("SELECT * FROM realtime_records WHERE object_id = %s ORDER BY time;", (object['id'],), simplify=False, debug=True)
            is_delete = False

            if len(records) > min_length:
                time0 = object['time_start']
                order = 6
                Cx, Cy = getObjectTrajectory(object, records, order=order)
                dx, dy = [], []

                for record in records:
                    dt = (record['time'] - time0).total_seconds()
                    x0 = round(sum([Cx[n]*dt**n for n in xrange(order)]))
                    y0 = round(sum([Cy[n]*dt**n for n in xrange(order)]))

                    dx.append(record['x'] - x0)
                    dy.append(record['y'] - y0)

                sd = numpy.sqrt(numpy.std(dx)**2 + numpy.std(dy)**2)

                if sd > 10:
                    print object['id'], "stddev=%g, probably noise event" % sd
                    is_delete = True

                if not is_delete:
                    time0, x0, y0, size_x, size_y = records[0]['time'], records[0]['x'], records[0]['y'], 300, 300
                    dark = self.favor2.find_image(time0, type='dark', channel_id=object['channel_id'])
                    raws = self.find_raw_files(time0, time0, channel_id=object['channel_id'])

                    if raws:
                        fitsname = raws[0]['fullname']
                        fd, tmpname = tempfile.mkstemp(suffix='.fits')
                        os.close(fd)

                        try:
                            command = "./crop x0=%d y0=%d width=%d height=%d %s %s" % (round(x0), round(y0), round(size_x), round(size_y), fitsname, tmpname)
                            if dark:
                                command = command + " dark=%s" % (dark)

                            os.system(command)

                            img = pyfits.getdata(tmpname)

                            mask = ndimage.gaussian_filter(img, 1)
                            thresh = np.percentile(mask, 95)
                            _,mask = cv2.threshold(mask.astype(np.float32), thresh, 1, cv2.THRESH_BINARY)
                            lines = cv2.HoughLines(mask.astype(np.uint8), 1, np.pi*1/180, int(0.5*size_x))

                            if lines is not None and lines.shape[1]:
                                rho, theta = lines[0][0]
                                x = 0.5*size_x
                                y = 0.5*size_y
                                a = y*np.cos(theta) - x*np.sin(theta)
                                b = rho - x*np.cos(theta) - y*np.sin(theta)

                                if abs(b) > 20:
                                    print object['id'], "line found but a=%g b=%g - not a laser" % (a, b)
                                else:
                                    print object['id'], "probably laser event"
                                    is_delete = True
                            else:
                                # pyfits.writeto('out_image.fits', img, clobber=True)
                                # pyfits.writeto('out_mask.fits', mask.astype(np.uint8), clobber=True)
                                pass
                        except:
                            import traceback
                            traceback.print_exc()
                            pass

                        os.unlink(tmpname)

            if is_delete:
                self.favor2.delete_object(object['id'])
                pass

    def extractStars(self, night='all', itype='survey', reprocess=False):
        where = []
        where_opts = []

        where.append("type = %s")
        where_opts.append(itype)

        where.append("filter = get_filter_id(%s)")
        where_opts.append("Clear")

        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)

        if self.channel_id:
            where.append("channel_id = %s")
            where_opts.append(self.channel_id)

        if not reprocess:
            where.append("id NOT IN (SELECT id FROM frames)")

        where_string = " AND ".join(where)
        #if where_string:
        #    where_string = "AND " + where_string

        res = self.favor2.query("SELECT * FROM images WHERE %s ORDER BY time DESC;" % (where_string), where_opts, simplify=False)

        print "%d frames to process" % len(res)

        for r in res:
            filename = fix_remote_path(r['filename'], r['channel_id'])
            catname = filename + ".txt"
            if os.path.isfile(catname) and not reprocess:
                #print catname
                continue

            print r['id'], filename

            command = "./extract -header -keep_psf -keep_simple %s %s" % (filename, catname)

            darkname = self.favor2.find_image(r['time'], 'dark', channel_id=r['channel_id'])
            flatname = self.favor2.find_image(r['time'], 'flat', channel_id=r['channel_id'])

            if darkname:
                command += " dark=%s" % fix_remote_path(darkname, r['channel_id'])
            if flatname:
                command += " flat=%s" % fix_remote_path(flatname, r['channel_id'])

            #print command
            os.system(command)

            if os.path.isfile(catname):
                # Successfully extracted
                # if self.favor2.query('SELECT TRUE FROM records WHERE frame = %s LIMIT 1', (r['id'], ), simplify=True):
                #     if reprocess:
                #         self.favor2.query('DELETE FROM records WHERE frame = %s', (r['id'], ))
                #     else:
                #         continue
                if self.favor2.query('SELECT TRUE FROM frames WHERE id = %s LIMIT 1', (r['id'], ), simplify=True):
                    if reprocess:
                        self.favor2.query('DELETE FROM records WHERE frame = %s', (r['id'], ))
                    else:
                        continue

                obj = get_objects_from_file(filename, simple=False)
                header = pyfits.getheader(filename, -1)
                wcs = pywcs.WCS(header)
                ra0, dec0, sr0 = ss.get_frame_center(header=header, wcs=wcs)
                cat = self.favor2.get_stars(ra0, dec0, sr0, catalog='pickles', limit=100000)

                quality = calibrate_objects(obj, cat, sr=15.0/3600)

                if quality is None:
                    continue

                # Bulk insert into DB
                s = StringIO.StringIO()
                s.writelines(["%s\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%d\t%d\t%d\t%g\t%g\t%d\t%g\t%g\n" % (r['time'], obj['ra'][i], obj['dec'][i], obj['mag'][i]+obj['mag0'][i], obj['magerr'][i], obj['Cbv'][i], obj['Cvr'][i], quality, r['id'], r['filter'], r['channel_id'], obj['x'][i], obj['y'][i], obj['flags'][i], obj['flux'][i], obj['fluxerr'][i]) for i in xrange(len(obj['ra'])) if obj['mag0'][i]])
                s.seek(0)
                cur = self.favor2.conn.cursor()
                cur.copy_from(s, 'records', columns=('time','ra','dec','mag','mag_err', 'Cbv', 'Cvr', 'quality', 'frame', 'filter', 'channel_id', 'x', 'y', 'flags', 'flux', 'flux_err'))
                self.favor2.conn.commit()

                # Update frames table
                if reprocess:
                    self.favor2.query("DELETE FROM frames WHERE id=%s", (r['id'],))
                self.favor2.query("INSERT INTO frames (id,channel_id,time,filter,ra0,dec0,cbv,cvr,quality) VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s)", [r['id'], r['channel_id'], r['time'], r['filter'], r['ra0'], r['dec0'], obj['Cbv'][0], obj['Cvr'][0], quality])

                #break

    def findTransients(self, night='all', type='survey', filter='Clear', id=0, reprocess=False):
        where = [];
        where_opts = [];

        if not reprocess:
            where.append("(keywords->'transients_extracted') IS NULL")
        if id:
            where.append("id = %s")
            where_opts.append(int(id))
        if type:
            where.append("type = %s")
            where_opts.append(type)
        if filter:
            where.append("filter = get_filter_id(%s)")
            where_opts.append(filter)
        if night and night != 'all':
            where.append("night = %s")
            where_opts.append(night)
        if self.channel_id:
            where.append("channel_id = %s")
            where_opts.append(self.channel_id)

        if where:
            where_string = "WHERE " + " AND ".join(where)
        else:
            where_string = ""

        res = self.favor2.query("SELECT * FROM images %s ORDER BY time;" % (where_string), where_opts, simplify=False)

        for r in res:
            filename = r['filename']
            filename = fix_remote_path(filename, channel_id=r['channel_id'])
            transients = find_transients_in_frame(filename, favor2=self.favor2, verbose=True)

            self.favor2.conn.autocommit = False

            if reprocess:
                self.favor2.query("DELETE FROM survey_transients WHERE frame_id=%s", (r['id'], ))

            for transient in transients:
                self.favor2.query("INSERT INTO survey_transients (channel_id, frame_id, filter, time, night, ra, dec, flux, flux_err, x, y, flags, simbad, mpc, preview) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)", (r['channel_id'], r['id'], r['filter'], r['time'], r['night'], transient['ra'], transient['dec'], transient['flux'], transient['flux_err'], transient['x'], transient['y'], transient['flags'], transient['simbad'], transient['mpc'], transient['preview']))

            self.favor2.query("UPDATE images SET keywords = keywords || 'transients_extracted=>1' WHERE id=%s", (r['id'],))

            self.favor2.conn.commit()
            self.favor2.conn.autocommit = True

    def markReliableTransients(self, night='all'):
        where = "";
        where_opts = []

        if night and night != 'all':
            where = "AND t1.night = %s AND t2.night = %s"
            where_opts = [night, night]

        res = self.favor2.query("SELECT t1.id FROM survey_transients t1, survey_transients t2 WHERE (t1.params->'reliable') IS NULL AND q3c_join(t1.ra, t1.dec, t2.ra, t2.dec, 0.01) AND t1.time < t2.time AND (t2.time - t1.time) < '30 minutes'::interval AND (t2.time - t1.time) > '1 minute'::interval %s" % (where), where_opts, simplify=False)

        for r in res:
            self.favor2.query("UPDATE survey_transients SET params='reliable=>1'::hstore WHERE id=%s", (r['id'],))

# Main
if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-b', '--base', help='base path, default to ./', action='store', dest='base', default='')
    parser.add_option('-d', '--dbname', help='database name, defaults to favor2', action='store', dest='dbname', default='favor2')
    parser.add_option('-H', '--dbhost', help='database hostname, defaults to none', action='store', dest='dbhost', default='')
    parser.add_option('-r', '--reprocess', help='Process all objects, even already processed', action='store_true', dest='reprocess', default=False)
    parser.add_option('-C', '--coadd-pointings', help='Coadd images for separate pointings', action='store_true', dest='coadd_pointings', default=False)
    parser.add_option('-n', '--night', help='Night to process, defaults to all', action='store', dest='night', default='')
    parser.add_option('-i', '--id', help='Object id, default to 0', action='store', dest='id', default=0)
    parser.add_option('-s', '--state', help='Object state, default to all', action='store', dest='state', default='')
    parser.add_option('--channel-id', help='Channel id, default to 0', action='store', dest='channel_id', default=0)
    parser.add_option('--deltat', help='Delta T, defaults to 1 s', action='store', dest='deltat', default=1)
    parser.add_option('-c', '--clean', help='Clean the images', action='store_true', dest='clean', default=False)
    parser.add_option('-k', '--keep-orig', help='Keep the original frames alongside with cropped ones', action='store_true', dest='keep_orig', default=False)
    parser.add_option('--merge-meteors', help='Merge close meteor events', action='store_true', dest='merge_meteors', default=False)
    parser.add_option('--filter-laser', help='Filter laser-caused meteor events', action='store_true', dest='filter_laser', default=False)
    parser.add_option('--filter-satellites', help='Filter satellites', action='store_true', dest='filter_satellites', default=False)
    parser.add_option('--filter-satellite-meteors', help='Filter satellite-caused meteor events', action='store_true', dest='filter_satellite_meteors', default=False)
    parser.add_option('--analyze-meteors', help='Analyze meteor events', action='store_true', dest='analyze_meteors', default=False)
    parser.add_option('--filter-flashes', help='Filter flashes', action='store_true', dest='filter_flashes', default=False)
    parser.add_option('--process-multicolor-meteors', help='Process multicolor meteors', action='store_true', dest='process_multicolor_meteors', default=False)
    parser.add_option('--min-length', help='Minimal object length for filtering, defaults to 35', action='store', dest='min_length', default=35)
    parser.add_option('--size', help='Cut size, default to 300', action='store', dest='cutsize', default=300)
    parser.add_option('-p', '--preview', help='Minimal processing for preview only', action='store_true', dest='preview', default=False)
    parser.add_option('-e', '--extract', help='Extract stars', action='store_true', dest='extract', default=False)
    parser.add_option('--find-transients', help='Find survey transients', action='store_true', dest='find_transients', default=False)
    parser.add_option('--mark-reliable-transients', help='Mark reliable survey transients', action='store_true', dest='mark_reliable_transients', default=False)

    (options, args) = parser.parse_args()

    pp = postprocessor(base=options.base, dbname = options.dbname, dbhost=options.dbhost, channel_id=options.channel_id)

    pp.deltat = float(options.deltat)
    pp.clean = options.clean
    pp.keep_orig = options.keep_orig
    pp.min_length = int(options.min_length)
    pp.preview = options.preview
    pp.cutsize = int(options.cutsize)

    if options.merge_meteors:
        pp.mergeMeteors(night=options.night)

    elif options.filter_laser:
        pp.filterLaserEvents(night=options.night)

    elif options.filter_satellites:
        pp.filterSatellites(night=options.night)

    elif options.filter_satellite_meteors:
        pp.filterSatelliteMeteors(night=options.night, state=options.state)

    elif options.analyze_meteors:
        pp.analyzeMeteors(night=options.night, id=options.id, reprocess=options.reprocess)

    elif options.process_multicolor_meteors:
        pp.processMulticolorMeteors(night=options.night, id=options.id, reprocess=options.reprocess)

    elif options.filter_flashes:
        pp.filterFlashes(night=options.night, id=options.id)

    elif options.coadd_pointings:
        pp.coaddPointings(night=options.night, all=options.process_all)

    elif options.extract:
        pp.extractStars(night=options.night, reprocess=options.reprocess)

    elif options.find_transients:
        pp.findTransients(night=options.night, reprocess=options.reprocess, id=options.id)

    elif options.mark_reliable_transients:
        pp.markReliableTransients(night=options.night)

    else:
        pp.processObjects(id=options.id, reprocess=options.reprocess, state=options.state, night=options.night)
