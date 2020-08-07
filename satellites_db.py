#!/usr/bin/env python

import os, posixpath, re, glob, datetime
import numpy as np

from favor2 import Favor2
import ephem
import requests

def parse_tle(tle):
    s = tle.split()

    mjd = float(s[0])
    date = ephem.Date(2400000 + mjd - 2415020.0).__str__()

    tle = {
        'mjd': mjd,
        'time': ephem.Date(2400000 + mjd - 2415020.0).datetime(),
        'tle': ' '.join(s[1:]),
        'period': 1440.0/float(s[1]),
        'inclination': float(s[2]),
        'eccentricity': float(s[3])
    }

    return tle

def read_satellite_track_file(filename):
    print "Reading %s" % filename

    with open(filename, 'r') as file:
        lines = file.readlines()

        if not lines or len(lines) < 2:
            return None

        track = {}

        track['id'] = int(posixpath.splitext(posixpath.split(filename)[-1])[0])

        track['catalogue_id'] = int(lines[0].split()[0])
        track['name'] = " ".join(lines[0].split()[1:])

        s = lines[1].split()
        if s:
            track['country'] = s[0]
        else:
            track['country'] = ''

        if lines[2].split():
            track['launch_date'] = datetime.datetime.strptime(lines[2].split()[0], "%Y-%m-%d")
        else:
            track['launch_date'] = None

        track['catalogue'] = int(lines[3].split()[0])
        track['tle'] = " ".join(lines[3].split()[1:])

        tle = parse_tle(track['tle'])
        track['orbit_inclination'] = tle['inclination']
        track['orbit_period'] = tle['period']
        track['orbit_eccentricity'] = tle['eccentricity']

        s = lines[4].split()

        track['ang_vel_ra'] = float(s[0])
        track['ang_vel_dec'] = float(s[1])

        s = lines[5].split()

        track['age'] = float(s[0])
        track['transversal_shift'] = float(s[1])
        track['transversal_rms'] = float(s[2])
        track['binormal_shift'] = float(s[3])
        track['binormal_rms'] = float(s[4])

        s = lines[6].split()

        track['variability'] = int(s[0])
        track['variability_period'] = float(s[1])

        records = []
        object_ids = {}

        for line in lines[7:]:
            rec = {}
            s = line.split()

            rec['object_id'] = int(s[0])
            rec['time'] = datetime.datetime.strptime(" ".join(s[1:7]), "%Y %m %d %H %M %S.%f")
            rec['stdmag'] = float(s[7])
            rec['phase'] = float(s[8])
            rec['distance'] = float(s[9])
            rec['penumbra'] = float(s[10])

            object_ids[int(s[0])] = 1

            records.append(rec)

        track['records'] = records
        track['object_ids'] = object_ids.keys()

        return track

from lomb import PDM
def refine_period(x, y, P0, detrend=True):
    if detrend:
        x = np.array(x)
        y = np.array(y)

        f = np.polyfit(x, y, 2)
        y0 = np.polyval(f, x)

        y = y - y0

    pp = np.arange(P0*0.9, P0*1.1, P0*0.001)
    pdm = PDM(x, y, 1.0/pp)
    pmin = pp[pdm == np.min(pdm)][0]

    pp = np.arange(pmin*0.99, pmin*1.01, pmin*0.0001)
    pdm = PDM(x, y, 1.0/pp)
    pmin = pp[pdm == np.min(pdm)][0]

    return pmin

def refine_track_period(track):
    t0 = track['records'][0]['time']

    x = [(r['time'] - t0).total_seconds() for r in track['records'] if r['penumbra'] == 0]
    y = [r['stdmag'] for r in track['records'] if r['penumbra'] == 0]

    if len(x) < 10:
        print "Not enough non-penumbra points (%d) to refine period" % len(x)
        return track['variability_period']
    elif np.max(x) < 2.0*track['variability_period']:
        print "Not enough points for refining: %g < 2.0 * %g" % (np.max(x), track['variability_period'])
        return track['variability_period']
    else:
        return refine_period(x, y, track['variability_period'])

def process_satellite_dir(dirname='.'):
    print "Reading satellites data from %s:" % dirname

    files = glob.glob(posixpath.join(dirname, '????????.txt'))
    files.sort()

    for file in files:
        track = read_satellite_track_file(file)
        if track['variability'] == 2:
            print "P0=%g" % track['variability_period']
            track['variability_period'] = refine_track_period(track)
            print "P=%g" % track['variability_period']

        process_satellite_track(track)

def process_satellite_track(track):
    f = Favor2()

    print "Processing satellite track %d" % track['id']

    # Find the satellite
    try:
        sat = f.query("SELECT * FROM satellites WHERE catalogue=%s AND catalogue_id=%s;", (track['catalogue'], track['catalogue_id']), simplify=True)
        if not sat:
            # Create new one, if not exists
            sat = f.query("INSERT INTO satellites (catalogue, catalogue_id, name, launch_date, country, orbit_inclination, orbit_period, orbit_eccentricity) VALUES (%s, %s, %s, %s, %s, %s, %s, %s) RETURNING *;", (track['catalogue'], track['catalogue_id'], track['name'], track['launch_date'], track['country'], track['orbit_inclination'], track['orbit_period'], track['orbit_eccentricity']))
    except Exception, ex:
        print ex.message
        return
        pass

    # Create satellite track if necessary
    try:
        f.query("DELETE FROM satellite_tracks WHERE id=%s;", (track['id'],))

        f.query("INSERT INTO satellite_tracks (id, satellite_id, tle, age, transversal_shift, transversal_rms, binormal_shift, binormal_rms, variability, variability_period) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s);", (track['id'], sat['id'], track['tle'], track['age'], track['transversal_shift'], track['transversal_rms'], track['binormal_shift'], track['binormal_rms'], track['variability'], track['variability_period']))

        # We insert track records only if the track is just created
        f.conn.autocommit = False
        for rec in track['records']:
            # record = f.query("SELECT r.*, o.channel_id as channel_id FROM realtime_records r, realtime_objects o WHERE r.object_id=%s AND r.time=%s AND r.object_id = o.id LIMIT 1;", (rec['object_id'], rec['time']))

            # Insert track point, if not done already
            try:
                #f.query("INSERT INTO satellite_records (satellite_id, track_id, record_id, object_id, time, ra, dec, filter, mag, channel_id, flags, stdmag, phase, distance, penumbra) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s);", (sat['id'], track['id'], record['id'], record['object_id'], record['time'], record['ra'], record['dec'], record['filter'], record['mag'], record['channel_id'], record['flags'], rec['stdmag'], rec['phase'], rec['distance'], rec['penumbra']))
                f.query("INSERT INTO satellite_records (satellite_id, track_id, record_id, object_id, time, ra, dec, filter, mag, channel_id, flags, stdmag, phase, distance, penumbra) (SELECT %s, %s, r.id, r.object_id, r.time, r.ra, r.dec, r.filter, r.mag, o.channel_id as channel_id, r.flags, %s, %s, %s, %s FROM realtime_records r, realtime_objects o WHERE r.object_id=%s AND r.time=%s AND r.object_id = o.id);", (sat['id'], track['id'], rec['stdmag'], rec['phase'], rec['distance'], rec['penumbra'], rec['object_id'], rec['time']))
            except Exception, ex:
                print ex.message
                pass

        # Now update the satellite variability flag
        f.query("UPDATE satellites s SET variability = (SELECT MAX(variability) FROM satellite_tracks WHERE satellite_id=s.id), variability_period = (SELECT variability_period FROM satellite_tracks WHERE satellite_id=s.id AND variability=2 ORDER BY id DESC LIMIT 1), orbit_inclination=(SELECT split_part(tle, ' ', 3)::FLOAT FROM satellite_tracks WHERE satellite_id=s.id ORDER BY id DESC LIMIT 1), orbit_period=1440.0/(SELECT split_part(tle, ' ', 2)::FLOAT FROM satellite_tracks WHERE satellite_id=s.id ORDER BY id DESC LIMIT 1), orbit_eccentricity=(SELECT split_part(tle, ' ', 4)::FLOAT FROM satellite_tracks WHERE satellite_id=s.id ORDER BY id DESC LIMIT 1) WHERE id=%s", (sat['id'],))

        f.conn.commit()
        f.conn.autocommit = True

    except Exception, ex:
        print ex.message
        pass

def update_satcat():
    favor2 = Favor2()

    res = favor2.query('SELECT * FROM satellites WHERE (catalogue=1 OR catalogue=3) AND (iname IS NULL OR rcs IS NULL)')
    ids = [_['catalogue_id'] for _ in res]
    print ids

    res0 = favor2.query('SELECT * FROM satellites WHERE (catalogue=1 OR catalogue=3)')
    ids0 = [_['catalogue_id'] for _ in res0]
    #print ids0

    lines = requests.get('https://celestrak.com/pub/satcat.txt')

    print "satcat downloaded"

    for line in lines.text.splitlines():
        iname = line[0:11]
        norad = line[13:18]
        rcs = line[119:127]

        iname, norad, rcs = [_.strip() for _ in iname, norad, rcs]

        norad = int(norad)
        rcs = float(rcs) if rcs and rcs != 'N/A' else None

        if norad not in ids0:
            continue

        print norad, iname, rcs
        favor2.query('UPDATE satellites SET rcs=%s WHERE (catalogue=1 OR catalogue=3) AND catalogue_id=%s AND (rcs is NULL or rcs!=%s)', (rcs, norad, rcs))

        if norad not in ids:
            continue

        if iname:
            favor2.query('UPDATE satellites SET iname=%s WHERE (catalogue=1 OR catalogue=3) AND catalogue_id=%s', (iname, norad))

    res = favor2.query('SELECT * FROM satellites WHERE (catalogue=1 OR catalogue=3) AND (iname IS NULL OR rcs IS NULL)')
    ids = [_['catalogue_id'] for _ in res]
    print ids

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-d', '--dir', help='directory to scan for satellite tracks', action='store', dest='dir', default='satellites')

    (options,args) = parser.parse_args()

    process_satellite_dir(options.dir)
    update_satcat()
