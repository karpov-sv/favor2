#!/usr/bin/env python

import psycopg2, psycopg2.extras
import numpy as np

from astropy.io import fits as pyfits
from astropy import wcs as pywcs

import tempfile, datetime, posixpath, shutil, re, os

import ephem
import socket
import re

def get_time_from_string(filename):
    m = re.match("^(\d\d)\.(\d\d)\.(\d\d\d\d) (\d\d):(\d\d):(\d\d).(\d\d\d)", filename)
    if m:
        t = [int(m.group(i)) for i in range(1,8)]
        return datetime.datetime(t[2], t[1], t[0], t[3], t[4], t[5], t[6]*1000)
    else:
        return None

def get_time_from_filename(filename):
    filename = posixpath.split(filename)[-1]
    m = re.match("^(\d\d\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(\d\d\d)", filename)
    if m:
        t = [int(m.group(i)) for i in range(1,8)]
        return datetime.datetime(t[0], t[1], t[2], t[3], t[4], t[5], t[6]*1000)
    else:
        return None

def split_path(filename):
    paths = []

    while posixpath.basename(filename):
        paths.append(posixpath.basename(filename))
        filename = posixpath.dirname(filename)

    paths.reverse()

    return paths

def fix_remote_path(filename, channel_id=0):
    if not filename:
        return None

    if not channel_id:
        m = re.match("^\d{8}_\d{6}_(\d)\.", posixpath.split(filename)[-1])
        if m:
            channel_id = m.group(1)

    for name in [filename, posixpath.join('/data', filename), posixpath.join('/MMT/%d' % int(channel_id), filename)]:
        if posixpath.exists(name):
            return name

    return None

# Guess the channel id from hostname
def get_channel_id():
    m = re.match("^mmt(\d+)", socket.gethostname())

    if m:
        id = int(m.group(1))

        if id:
            #print "Channel ID=%d" % id
            channel_id = id
        else:
            channel_id = 0

    return channel_id

def get_time_from_night(night):
    m = re.match("^(\d\d\d\d)_(\d\d)_(\d\d)", night)
    if m:
        t = [int(m.group(i)) for i in range(1,4)]
        return datetime.datetime(t[0], t[1], t[2])

def parse_time(string):
    return datetime.datetime.strptime(string, '%d.%m.%Y %H:%M:%S.%f')

import smtplib
from email.message import Message
from email.mime.audio import MIMEAudio
from email.mime.text import MIMEText
from email.mime.image import MIMEImage
from email.mime.base import MIMEBase
from email.mime.multipart import MIMEMultipart
from email import encoders
import mimetypes

def send_email(message, to='karpov.sv@gmail.com', subject=None, sender='MMT', attachments=[]):
    if attachments:
        msg = MIMEMultipart()
        msg.attach(MIMEText(message))

        for filename in attachments:
            with open(filename, 'r') as fp:
                ctype, encoding = mimetypes.guess_type(filename)
                if ctype is None or encoding is not None:
                    # No guess could be made, or the file is encoded (compressed), so
                    # use a generic bag-of-bits type.
                    ctype = 'application/octet-stream'

                maintype, subtype = ctype.split('/', 1)

                if maintype == 'text':
                    data = MIMEText(fp.read(), _subtype=subtype)
                elif maintype == 'image':
                    data = MIMEImage(fp.read(), _subtype=subtype)
                elif maintype == 'audio':
                    data = MIMEAudio(fp.read(), _subtype=subtype)
                else:
                    data = MIMEBase(maintype, subtype)
                    data.set_payload(fp.read())
                    encoders.encode_base64(data)

            data.add_header('Content-Disposition', 'attachment', filename=posixpath.split(filename)[-1])
            msg.attach(data)

    else:
        msg = MIMEText(message)

    msg['Subject'] = subject
    msg['From'] = sender
    #msg['To'] = to

    s = smtplib.SMTP('localhost')
    s.sendmail('karpov@mmt0.sonarh.ru', to, msg.as_string())
    s.quit()

class Favor2:
    def __init__(self, dbname='favor2', dbhost='', dbport=0, dbuser='', dbpassword='', base='.', readonly=False,
                 latitude=43.649861, longitude=41.4314722, elevation=2030):
        connstring = "dbname=" + dbname
        if dbhost:
            connstring += " host="+dbhost
        if dbport:
            connstring += " port=%d" % dbport
        if dbuser:
            connstring += " user="+dbuser
        if dbpassword:
            connstring += " password='%s'" % dbpassword

        self.connect(connstring, readonly)

        self.base = base
        self.raw_base = posixpath.join(base, "IMG")
        self.archive_base = posixpath.join(base, "ARCHIVE")

        self.obs = ephem.Observer()
        self.obs.lat = latitude*ephem.pi/180.0
        self.obs.lon = longitude*ephem.pi/180.0
        self.obs.elevation = elevation

        self.moon = ephem.Moon()
        self.sun = ephem.Sun()

    def connect(self, connstring, readonly=False):
        self.conn = psycopg2.connect(connstring)
        self.conn.autocommit = True
        self.conn.set_session(readonly=readonly)
        psycopg2.extras.register_hstore(self.conn)

        self.connstring = connstring
        self.readonly = readonly

    def query(self, string="", data=(), simplify=True, debug=False, array=False):
        if self.conn.closed:
            print "Re-connecting to BD"
            self.connect(self.connstring, self.readonly)

        cur = self.conn.cursor(cursor_factory = psycopg2.extras.DictCursor)

        if debug:
            print cur.mogrify(string, data)

        if data:
            cur.execute(string, data)
        else:
            cur.execute(string)

        try:
            result = cur.fetchall()
            # Simplify the result if it is simple
            if array:
                # Code from astrolibpy, https://code.google.com/p/astrolibpy
                strLength = 10
                __pgTypeHash = {
                    16:bool,18:str,20:'i8',21:'i2',23:'i4',25:'|S%d'%strLength,700:'f4',701:'f8',
                    1042:'|S%d'%strLength,#character()
                    1043:'|S%d'%strLength,#varchar
                    1700:'f8' #numeric
                }

                desc = cur.description
                names = [d.name for d in desc]
                formats = [__pgTypeHash[d.type_code] for d in desc]

                table = np.recarray(shape=(cur.rowcount,), formats=formats, names=names)

                for i,v in enumerate(result):
                    table[i] = tuple(v)

                return table
            elif simplify and len(result) == 1:
                if len(result[0]) == 1:
                    return result[0][0]
                else:
                    return result[0]
            else:
                return result
        except:
            # Nothing returned from the query
            return None

    def tempdir(self, prefix='favor2'):
        return tempfile.mkdtemp(prefix=prefix)

    def find_image(self, time, type='avg', channel_id=-1, before=True):
        #res = self.query('select find_image(%s, %s, %s);', (time, type, channel_id))

        if before:
            res = self.query('select filename from images where time <= %s and type=%s and channel_id=%s order by time desc limit 1;', (time, type, channel_id))
        else:
            res = None

        if not res:
            res = self.query('select filename from images where time > %s and type=%s and channel_id=%s order by time asc limit 1;', (time, type, channel_id))

        return res

    def get_night(self, t):
        et = t - datetime.timedelta(hours=12)
        return "%04d_%02d_%02d" % (et.year, et.month, et.day)

    # Find sequences of consecutive images with no significant gaps and close on the sky
    def find_pointings(self, night=None):
        if not night:
            res = self.query("WITH s AS (SELECT filename,time,night,channel_id, CASE WHEN ((time-lag(time) OVER w"
                             " BETWEEN '1 second'::interval AND '30 seconds'::interval) AND"
                             " q3c_dist(ra0, dec0, lag(ra0) OVER w, lag(dec0) OVER w) < 1) THEN 0 ELSE 1 END"
                             " AS flag FROM images WHERE type='avg' AND (ra0 != 0 OR dec0 != 0) WINDOW w AS (ORDER BY channel_id,time)),"
                             "grp AS (SELECT *,sum(flag) OVER (ORDER BY channel_id,time) AS gid FROM s) "
                             "SELECT count(filename) AS length, min(filename) AS first, min(night) as night,"
                             " min(channel_id) as channel_id,"
                             " array_agg(filename) as filenames, min(time) as time FROM grp GROUP BY gid;", simplify=False)
        else:
            res = self.query("WITH s AS (SELECT filename,time,night,channel_id, CASE WHEN ((time-lag(time) OVER w"
                             " BETWEEN '1 second'::interval AND '30 seconds'::interval) AND"
                             " q3c_dist(ra0, dec0, lag(ra0) OVER w, lag(dec0) OVER w) < 1) THEN 0 ELSE 1 END"
                             " AS flag FROM images WHERE type='avg' AND (ra0 != 0 OR dec0 != 0) AND night=%s WINDOW w AS (ORDER BY channel_id,time)),"
                             "grp AS (SELECT *,sum(flag) OVER (ORDER BY channel_id,time) AS gid FROM s) "
                             "SELECT count(filename) AS length, min(filename) AS first, min(night) as night,"
                             " min(channel_id) as channel_id,"
                             " array_agg(filename) as filenames, min(time) as time FROM grp GROUP BY gid;", (night,), simplify=False)

        return [{'first':r['first'], 'length':r['length'], 'night':r['night'], 'time':r['time'], 'channel_id':r['channel_id'], 'filenames':r['filenames']} for r in res]

    # Coadd set of images from the image archive
    # filenames are from the DB and are relative to self.base
    # filename is relative to current dir or absolute
    def coadd_archive_images(self, filenames, filename='coadd.fits'):
        dir = self.tempdir()

        filenames.sort()
        tmpnames = []

        print "Coadding: %s = %s" % (filename, " + ".join(filenames))

        for name in filenames:
            tmpname = posixpath.join(dir, posixpath.split(name)[-1])
            self.prepare_image(posixpath.join(self.base, name), tmpname)
            tmpnames.append(tmpname)

        os.system("swarp -VERBOSE_TYPE QUIET -COMBINE_TYPE AVERAGE -IMAGEOUT_NAME %s %s" % (filename, " ".join(tmpnames)))

        # Now add some keywords to resulting image header
        img = pyfits.open(filename, mode='update')
        img1 = pyfits.open(posixpath.join(self.base, filenames[0]))

        img[-1].header['TIME'] = img1[-1].header['TIME']

        for name in filenames:
            img[-1].header['comment'] = 'coadd %s' % name

        img.flush()

        shutil.rmtree(dir)

    # Normalize image using dark and flat frames from the archive
    def prepare_image(self, filename, outname):
        print "Normalizing %s -> %s" % (filename, outname)

        img = pyfits.open(filename)
        time = get_time_from_string(img[-1].header['TIME'])
        channel_id = int(img[-1].header['CHANNEL_ID']) or -1

        dark_filename = self.find_image(time, 'dark', channel_id)
        if dark_filename:
            print "Subtracting DARK: %s" % (dark_filename)
            dark = pyfits.open(posixpath.join(self.base, dark_filename))

            if dark[-1].shape == img[-1].shape:
                img[-1].data -= dark[-1].data
                img[-1].header['comment'] = 'Dark frame subtracted: %s' % (dark_filename)

                flat_filename = self.find_image(time, 'flat', channel_id)
                if flat_filename:
                    print "Normalizing to FLAT: %s" % (flat_filename)
                    flat = pyfits.open(posixpath.join(self.base, flat_filename))

                    if flat[-1].shape == img[-1].shape:
                        img[-1].data *= np.mean(flat[-1].data - dark[-1].data)/(flat[-1].data - dark[-1].data)
                        img[-1].header['comment'] = 'Flatfield corrected: %s' % (flat_filename)

        img.writeto(outname, clobber=True)

    # Register the image in the database
    def register_image(self, filename, imtype='avg', time=None, channel_id=0, filter=None, ra0=None, dec0=None):
        print "Inserting %s (type=%s) into database" % (filename, imtype)

        img = pyfits.open(filename)
        h = img[-1].header

        if not time:
            time = get_time_from_string(h['TIME'])
        elif type(time) == str:
            time = get_time_from_string(time)

        if not channel_id:
            channel_id = h['CHANNEL ID']

        if not filter:
            filter = h['FILTER']

        night = self.get_night(time)
        width = img[-1].shape[1]
        height = img[-1].shape[0]

        if not ra0 or not dec0:
            w = pywcs.WCS(header=h)
            if w.wcs.has_cd():
                [ra0],[dec0] = w.all_pix2world([0.5*width], [0.5*height], 1)
            else:
                ra0,dec0 = 0,0

        # header to dict
        keywords = dict(h)
        for kw in ["SIMPLE", "BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "EXTEND", "COMMENT", "BZERO", "BSCALE", "HISTORY"]:
            while keywords.pop(kw, None):
                pass

        # Fix the filename
        if '/MMT/' in filename:
            filename = filename[7:]
        elif '/data/' in filename:
            filename = filename[6:]

        # FIXME: escape quotes!
        keywords = ",".join(['"%s"=>"%s"'%(k,keywords[k]) for k in keywords.keys()])
        #print keywords

        self.query('DELETE FROM images WHERE type=%s AND filename=%s', (imtype, filename))

        self.query("INSERT INTO images (filename, night, channel_id, time, type, ra0, dec0, width, height, keywords, filter) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s::hstore, get_filter_id(%s))",
                   (filename, night, channel_id, time, imtype, ra0, dec0, width, height, keywords, filter))

    # Query catalogues
    def get_tycho2(self, ra0=0, dec0=0, sr0=0, limit=10000):
        res = self.query("SELECT ra, dec, 0.76*bt+0.24*vt as b , 1.09*vt-0.09*bt as v FROM tycho2 WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY b ASC LIMIT %s;", (ra0, dec0, sr0, limit), simplify=False)

        return {'ra':[x['ra'] for x in res],
                'dec':[x['dec'] for x in res],
                'b':[x['b'] for x in res],
                'v':[x['v'] for x in res]}

    def get_wbvr(self, ra0=0, dec0=0, sr0=0, limit=10000):
        res = self.query("SELECT ra, dec, b, v, r FROM wbvr WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY b ASC LIMIT %s;", (ra0, dec0, sr0, limit), simplify=False)

        return {'ra':[x['ra'] for x in res],
                'dec':[x['dec'] for x in res],
                'b':[x['b'] for x in res],
                'v':[x['v'] for x in res],
                'r':[x['r'] for x in res]}

    def get_nomad(self, ra0=0, dec0=0, sr0=0, limit=100000):
        res = self.query("SELECT ra, dec, b, v, r FROM nomad WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY b ASC LIMIT %s;", (ra0, dec0, sr0, limit), simplify=False)

        return {'ra':[x['ra'] for x in res],
                'dec':[x['dec'] for x in res],
                'b':[x['b'] for x in res],
                'v':[x['v'] for x in res],
                'r':[x['r'] for x in res]}

    def get_pickles(self, ra0=0, dec0=0, sr0=0, limit=100000):
        res = self.query("SELECT ra, dec, b, v, r FROM pickles WHERE q3c_radial_query(ra, dec, %s, %s, %s) ORDER BY b ASC LIMIT %s;", (ra0, dec0, sr0, limit), simplify=False)

        return {'ra':[x['ra'] for x in res],
                'dec':[x['dec'] for x in res],
                'b':[x['b'] for x in res],
                'v':[x['v'] for x in res],
                'r':[x['r'] for x in res]}

    def get_stars(self, ra0=0, dec0=0, sr0=0, limit=10000, catalog='tycho2', as_list=False, extra=[], extrafields=None):
        # Code from astrolibpy, https://code.google.com/p/astrolibpy
        strLength = 10
        __pgTypeHash = {
            16:bool,18:str,20:'i8',21:'i2',23:'i4',25:'|S%d'%strLength,700:'f4',701:'f8',
            1042:'|S%d'%strLength,#character()
            1043:'|S%d'%strLength,#varchar
            1700:'f8' #numeric
        }

        substr, order = "", "v"
        if catalog == 'tycho2':
            substr = "0.76*bt+0.24*vt as b , 1.09*vt-0.09*bt as v, 0 as r"
        elif catalog == 'twomass':
            order = "j"

        if extra and type(extra) == list:
            extra_str = " AND " + " AND ".join(extra)
        elif extra and type(extra) == str:
            extra_str = " AND " + extra
        else:
            extra_str = ""

        if extrafields:
            if substr:
                substr = substr + "," + extrafields
            else:
                substr = extrafields

        if substr:
            substr = "," + substr

        cur = self.conn.cursor(cursor_factory = psycopg2.extras.DictCursor)
        cur.execute("SELECT * " + substr + " FROM " + catalog + " cat WHERE q3c_radial_query(ra, dec, %s, %s, %s) " + extra_str + " ORDER BY " + order + " ASC LIMIT %s;", (ra0, dec0, sr0, limit))

        if as_list:
            return cur.fetchall()
        else:
            desc = cur.description
            names = [d.name for d in desc]
            formats = [__pgTypeHash[d.type_code] for d in desc]

            table = np.recarray(shape=(cur.rowcount,), formats=formats, names=names)

            for i,v in enumerate(cur.fetchall()):
                table[i] = tuple(v)

            return table

    # Store object' measurement in database
    def store_record(self, obj):
        id = self.query("INSERT INTO records (time, filter, ra, dec, mag, mag_err, flux, flux_err, x, y, flags) VALUES" +
                        " (%s, get_filter_id(%s), %s, %s, %s, %s, %s, %s, %s, %s, %s) RETURNING id",
                        (obj.get('time'), obj.get('filter'), obj.get('ra'), obj.get('dec'),
                         obj.get('mag'), obj.get('mag_err'), obj.get('flux'), obj.get('flux_err'),
                         obj.get('x'), obj.get('y'), obj.get('flags')))


    # Blind match image from file and get WCS
    def blind_match_file(self, filename=None, darkname=None, obj=None, outfile=None, order=2):
        dir = self.tempdir()
        wcs = None
        binname = None
        ext = 0

        for path in ['.', '/usr/local', '/opt/local']:
            if os.path.isfile(posixpath.join(path, 'astrometry', 'bin', 'solve-field')):
                binname = posixpath.join(path, 'astrometry', 'bin', 'solve-field')
                break

        if filename and darkname and posixpath.exists(filename) and posixpath.exists(darkname):
            image = pyfits.getdata(filename, -1)
            header = pyfits.getheader(filename, -1)
            dark = pyfits.getdata(darkname, -1)

            image = 1.0*image - dark

            filename = posixpath.join(dir, 'preprocessed.fits')

            pyfits.writeto(filename, image, clobber=True)

        if binname:
            extra = ""

            if not filename and obj:
                columns = [pyfits.Column(name='XIMAGE', format='1D', array=obj['x']),
                           pyfits.Column(name='YIMAGE', format='1D', array=obj['y']),
                           pyfits.Column(name='FLUX', format='1D', array=obj['flux'])]
                tbhdu = pyfits.BinTableHDU.from_columns(columns)
                filename = posixpath.join(dir, 'list.fits')
                tbhdu.writeto(filename, clobber=True)
                extra = "--x-column XIMAGE --y-column YIMAGE --sort-column FLUX --width %d --height %d" % (np.ceil(max(obj['x'])), np.ceil(max(obj['y'])))
            elif len(pyfits.open(filename)) > 1:
                # Compressed file, let's uncompress it
                newname = posixpath.join(dir, 'uncompressed.fits')
                img = pyfits.getdata(filename, -1)
                header = pyfits.getheader(filename, -1)
                pyfits.writeto(newname, img, header, clobber=True)
                filename = newname

            #os.system("%s -D %s --overwrite --no-fits2fits --no-plots --use-sextractor --objs 300 -t %d -l 30 %s %s >/dev/null 2>/dev/null" % (binname, dir, order, extra, filename))
            #os.system("%s -D %s --overwrite --no-fits2fits --no-plots --use-sextractor --objs 300 -t %d -l 30 %s %s" % (binname, dir, order, extra, filename))
            wcsname = posixpath.split(filename)[-1]
            fitsname = posixpath.join(dir, posixpath.splitext(wcsname)[0] + '.new')
            tmpname = posixpath.join(dir, posixpath.splitext(wcsname)[0] + '.tmp')
            wcsname = posixpath.join(dir, posixpath.splitext(wcsname)[0] + '.wcs')

            if not os.path.isfile(fitsname):
                os.system("%s -D %s --no-verify --overwrite --no-fits2fits --no-plots --use-sextractor --objs 300 -t %d -l 30 %s %s >/dev/null 2>/dev/null" % (binname, dir, order, extra, filename))
                #os.system("%s -D %s --no-verify --overwrite --no-fits2fits --no-plots --use-sextractor --objs 300 -t %d -l 30 %s" % (binname, dir, order, filename))

            if os.path.isfile(fitsname):
                shutil.move(fitsname, tmpname)
                os.system("%s -D %s --overwrite --no-fits2fits --no-plots --use-sextractor --objs 300 -t %d -l 30 %s >/dev/null 2>/dev/null" % (binname, dir, order, tmpname))
                #os.system("%s -D %s --overwrite --no-fits2fits --no-plots --use-sextractor --objs 300 -t %d -l 30 %s" % (binname, dir, order, tmpname))

                if os.path.isfile(wcsname):
                    header = pyfits.getheader(wcsname)
                    wcs = pywcs.WCS(header)

                    if outfile:
                        shutil.move(fitsname, outfile)
        else:
            print "Astrometry.Net binary not found"

        shutil.rmtree(dir)

        return wcs

    # Delete object and its archive, if present
    def delete_object(self, id=0):
        obj = self.query("SELECT night, get_realtime_object_state(state) as state_string FROM realtime_objects WHERE id=%s;", (id,))

        if obj:
            print "Deleting object %d" % int(id)

            self.query("DELETE FROM realtime_objects WHERE id = %s;", (id,))
            shutil.rmtree(posixpath.join(self.archive_base, obj['state_string'], obj['night'], str(id)), ignore_errors=True)

    def change_object_state(self, id=0, state='particle', old_state=None):
        obj = self.query("SELECT *, get_realtime_object_state(state) as state_string FROM realtime_objects WHERE id=%s;", (id,))

        if obj:
            if old_state:
                obj['state_string'] = old_state

            print "Changing object %d ('%s') state to '%s'" % (int(id), obj['state_string'], state)

            if obj['state_string'] != state:
                dir = posixpath.join(self.archive_base, obj['state_string'], obj['night'], str(id))
                newdir = posixpath.join(self.archive_base, state, obj['night'], str(id))

                if posixpath.isdir(dir):
                    print "Moving object archive images from %s to %s" % (dir, newdir)

                    # Rename, creating target dir hierarchy
                    try:
                        os.renames(dir, newdir)
                    except:
                        pass

                self.query("UPDATE realtime_objects SET state=get_realtime_object_state_id(%s) WHERE id=%s;", (state, int(id),))
                images = self.query("SELECT * FROM realtime_images WHERE object_id = %s;", (int(id), ))
                for image in images:
                    filename = image['filename']
                    newname = reduce(posixpath.join, [state if p==obj['state_string'] else p for p in split_path(filename)])
                    #print filename, newname
                    self.query("UPDATE realtime_images SET filename=%s WHERE id=%s AND filename=%s;", (newname, image['id'], filename))

    def log(self, text, id='news'):
        if text and id:
            time = datetime.datetime.utcnow()

            self.query("INSERT INTO log (time, id, text) VALUES (%s, %s, %s);", (time, id, text))

if __name__ == '__main__':
    f = Favor2()

    #f.coadd_archive_images(f.find_pointings('2014_01_28')[0]['filenames'])
    #f.prepare_image('tmp/1/20140201_205054.avg.fits', 'out.fits')
    #w = f.blind_match_file('out.fits')
