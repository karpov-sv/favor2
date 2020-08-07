#!/usr/bin/env python
import os
import posixpath
import datetime

import numpy as np
from coord import ang_sep

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from mpl_toolkits.basemap import Basemap

import matplotlib.pyplot as plt

from favor2 import Favor2
from tycho2 import tycho2

import ephem

def deg2rad(d):
    return d*np.pi/180.0

def rad2deg(r):
    return 180.0*r/np.pi

def norm(x):
    return np.sqrt(np.sum(x**2))

def radectoxyz(ra_deg, dec_deg):
    ra  = deg2rad(ra_deg)
    dec = deg2rad(dec_deg)
    xyz = np.array((np.cos(dec)*np.cos(ra),
                    np.cos(dec)*np.sin(ra),
                    np.sin(dec)))
    return xyz

def xyztoradec(xyz):
    ra = np.arctan2(xyz[1], xyz[0])
    ra += 2*np.pi * (ra < 0)
    dec = np.arcsin(xyz[2] / norm(xyz))
    return (rad2deg(ra), rad2deg(dec))

def cross(a, b):
    return np.array([
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    ])

def get_intersection(ra1_1, dec1_1, ra2_1, dec2_1,  ra1_2, dec1_2, ra2_2, dec2_2):
    '''
    Intersection of two great circles defined by two points each, first and second
    '''
    xyz1 = np.cross(radectoxyz(ra1_1, dec1_1), radectoxyz(ra2_1, dec2_1))
    xyz2 = np.cross(radectoxyz(ra1_2, dec1_2), radectoxyz(ra2_2, dec2_2))

    cp = np.cross(xyz1, xyz2)
    ra, dec = xyztoradec(cp)

    if np.dot(xyz1, np.cross(radectoxyz(ra, dec), radectoxyz(ra1_1, dec1_1))) < 0:
        cp = -cp
        ra, dec = xyztoradec(cp)

    if np.dot(xyz2, np.cross(radectoxyz(ra, dec), radectoxyz(ra1_2, dec1_2))) < 0:
        return None, None
    else:
        return ra, dec

def get_intersection_xyz(c1_1, c2_1, cross_1, c1_2, c2_2, cross_2):
    '''
    Intersection of two great circles defined by two points each, first and second
    '''
    xyz1 = cross_1 # np.cross(c1_1, c2_1)
    xyz2 = cross_2 # np.cross(c1_2, c2_2)

    cp = cross(xyz1, xyz2)

    if np.dot(xyz1, cross(cp, c1_1)) < 0:
        cp = -cp

    if np.dot(xyz2, cross(cp, c1_2)) > 0 and np.dot(cp, c1_1) > 0 and np.dot(cp, c1_2) > 0:
        return xyztoradec(cp)
    else:
        return None, None

def meteors_intersections_plot(file, favor2=None, night='all', size=800):
    if not favor2:
        favor2 = Favor2()

    where = ["state = get_realtime_object_state_id('meteor')"]
    where_opts = []

    where.append("night = %s")
    where_opts.append(night)

    where_string = " WHERE " + " AND ".join(where)

    Nobj_total = favor2.query("SELECT count(*) FROM realtime_objects %s;" % (where_string), where_opts, simplify=True)

    where.append("(params->'analyzed') = '1'")
    where.append("(params->'nframes')::int > 1")

    where_string = " WHERE " + " AND ".join(where)

    objects = favor2.query("SELECT * FROM realtime_objects %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

    Nobj = len(objects)

    print "%d meteors, %d analyzed" % (Nobj_total, Nobj)

    ra = []
    dec = []

    ra1 = np.empty(Nobj)
    dec1 = np.empty(Nobj)
    xyz1 = np.empty((Nobj, 3))
    ra2 = np.empty(Nobj)
    dec2 = np.empty(Nobj)
    xyz2 = np.empty((Nobj, 3))
    cross = np.empty((Nobj, 3))
    time = []

    for i,o in enumerate(objects):
        ra1[i] = float(o['params']['ra_start'])
        dec1[i] = float(o['params']['dec_start'])
        xyz1[i] = radectoxyz(ra1[i], dec1[i])
        ra2[i] = float(o['params']['ra_end'])
        dec2[i] = float(o['params']['dec_end'])
        xyz2[i] = radectoxyz(ra2[i], dec2[i])
        cross[i] = np.cross(xyz1[i], xyz2[i])
        time.append(o['time_start'])

    for i1 in xrange(Nobj - 1):
        for i2 in xrange(i1 + 1, Nobj):
            if abs((time[i1] - time[i2]).total_seconds()) > 10.0:
                # Filter out occational detection of single meteor by different channels
                ra_int, dec_int = get_intersection_xyz(xyz1[i1], xyz2[i1], cross[i1],
                                                       xyz1[i2], xyz2[i2], cross[i2])

                if ra_int != None and dec_int != None:
                    ra.append(ra_int)
                    dec.append(dec_int)

    # Plot the map of intersections
    fig = Figure(facecolor='white', dpi=72, figsize=(size/72, 0.6*size/72), tight_layout=True)
    ax = fig.add_subplot(111)

    # set up gnomonic map projection
    map = Basemap(projection='hammer', lat_0 = 0, lon_0 = 0, resolution = None, celestial = True, ax = ax)
    # draw the edge of the map projection region (the projection limb)
    map.drawmapboundary()

    # draw and label ra/dec grid lines every 30 degrees.
    degtoralabel = lambda deg : "$%d^h$" % int(deg/15)
    degtodeclabel = lambda deg : "%+d$^\circ$" % deg
    map.drawparallels(np.arange(-90, 90, 30), color='lightgray')#, fmt=degtodeclabel)
    map.drawmeridians(np.arange(0, 360, 45), color='lightgray')

    for h in [0,3,6,9,12,15,18,21]:
        try:
            x,y = map(h*15,0)
            if abs(x) < 1e10 and abs(y) < 1e10:
                ax.text(x,y, degtoralabel(h*15), color='black')
        except:
            pass

    # Place stars
    star_size = [10*(10**(-0.4*v)) for v in tycho2['v']]
    px, py = map(tycho2['ra'], tycho2['dec'])
    map.scatter(px, py, s=star_size, c='gray')

    # Intersections
    if Nobj:
        px, py = map(ra, dec)
        map.scatter(px, py, s=100.0/Nobj, color='red', marker='.', alpha=0.3)

    # Title etc
    ax.set_title("%d meteors, %d analyzed, %d intersections found" % (Nobj_total, Nobj, len(ra)))

    fig.suptitle("Meteors of night %s" % (night))

    # Return the image
    canvas = FigureCanvas(fig)
    canvas.print_png(file, bbox_inches='tight')

def get_solar_longitude(time = datetime.datetime.utcnow()):
    obs = ephem.Observer()
    obs.date = time
    sun = ephem.Sun()

    sun.compute(obs)

    return ephem.Ecliptic(sun).lon*180.0/ephem.pi

def angle_delta(a1, a2, degrees=True):
    if not degrees:
        a1 *= 180.0/np.pi
        a2 *= 180.0/np.pi

    res = np.abs(a1 - a2) % 360.0
    if res > 180.0:
        res = 360.0 - res

    return res

def night_to_time(night):
    return datetime.datetime.strptime(night, '%Y_%m_%d')

def meteors_radiants_plot(file, favor2=None, night='all', size=800, binsize=5, normalize=False, niter=100, vmin=-2, vmax=20):
    if not favor2:
        favor2 = Favor2()

    objects = favor2.query("SELECT * FROM meteors_view WHERE (night=%s or %s=%s) and (filter='Clear' or filter='R') ORDER BY time", (night,night,'all'), simplify=False)
    Nobj_total = len(objects)
    Nobj = 0

    # Build the grid with finer resolution than requested bin size
    step = 0.5*binsize
    ra0 = np.arange(360 + step, step=step)
    dec0 = np.arange(-90, 90 + step, step=step)
    ra0, dec0 = np.meshgrid(ra0, dec0)

    # The same grid, cartesian
    xyz = radectoxyz(ra0, dec0)

    # Accumulator array - unnormalized radiant map
    counts = np.zeros_like(ra0)

    step = 1000

    for istep in xrange(int(np.ceil(1.0*len(objects)/step))):
        first = istep*step
        last = min(len(objects), istep*step + step)

        xyz1,xyz2=[],[]
        for i in xrange(first, last):
            r = objects[i]
            if r['nframes'] > 1 and (i == 0 or (np.abs((r['time'] - objects[i-1]['time']).total_seconds() > 10.0))):
                # Two frames at least, direction should be determined
                xyz1.append(radectoxyz(r['ra_start'], r['dec_start']))
                xyz2.append(radectoxyz(r['ra_end'], r['dec_end']))
                Nobj += 1

        xyz1 = np.array(xyz1) # trail starts
        xyz2 = np.array(xyz2) # trail ends
        norm = np.cross(xyz1, xyz2) # normals to trail planes, from start to end
        norm = norm/np.linalg.norm(norm, axis=1)[:, np.newaxis] # ..., normalized

        cosdist = np.cos(np.pi*(90 - binsize)/180.0) # min cosine of distance from trail great circle

        dist = np.einsum('ai,ijk', norm, xyz) # cosines of (90 - distances from great circle)
        dot = np.einsum('ai,ijk', xyz1, xyz) # same hemisphere as meteor means dot > 0

        # Levi-Civita symbol for cross product
        eijk = np.zeros((3, 3, 3))
        eijk[0, 1, 2] = eijk[1, 2, 0] = eijk[2, 0, 1] = 1
        eijk[0, 2, 1] = eijk[2, 1, 0] = eijk[1, 0, 2] = -1
        norm_p = np.einsum('ijk,jyz,ak->aiyz', eijk, xyz, xyz1)
        dot_p = np.einsum('aiyz,ai->ayz', norm_p, norm) # dot_p means that trail is going outwards from the bin, not inwards

        for i in xrange(dist.shape[0]):
            idx = np.abs(dist[i]) < cosdist
            idx = np.logical_and(idx, dot[i] > 0)
            idx = np.logical_and(idx, dot_p[i] > 0)
            counts[idx] += 1

    # FIXME: normalization is currently broken!
    if normalize:
        # Simulate the distribution of zero hypothesis
        counts0 = np.zeros_like(counts)

        for ii in xrange(niter):
            xyz2 = np.random.uniform(-1, 1, size=xyz1.shape)
            norm = np.cross(xyz1, xyz2)
            norm = norm/np.linalg.norm(norm, axis=1)[:, np.newaxis]
            dist = np.einsum('ai,ijk', norm, xyz)
            norm_p = np.einsum('ijk,jyz,ak->aiyz', eijk, xyz, xyz1)
            dot_p = np.einsum('aiyz,ai->ayz', norm_p, norm)

            counts1 = np.zeros_like(counts)

            for i in xrange(norm.shape[0]):
                idx = np.abs(dist[i]) < cosdist
                idx = np.logical_and(idx, dot[i] > 0)
                idx = np.logical_and(idx, dot_p[i] > 0)
                counts0[idx] += 1

        counts0 /= niter

        # The statistics should be Poissonian, so let's subtract the mean and divide by stddev
        counts_norm = (counts - counts0) / np.sqrt(counts0)
        counts_norm[np.isnan(counts_norm)] = 0

    # Plot the map of intersections
    # FIXME: it gives some strange errors with colorbar, let's use pyplot instead
    #fig = Figure(facecolor='white', dpi=72, figsize=(size/72, 0.6*size/72), tight_layout=True)
    fig = plt.figure(facecolor='white', dpi=72, figsize=(size/72, 0.6*size/72), tight_layout=True)
    ax = fig.add_subplot(111)

    # set up gnomonic map projection
    map = Basemap(projection='hammer', lat_0 = 0, lon_0 = 0, resolution = None, celestial = True, ax = ax)
    # draw the edge of the map projection region (the projection limb)
    map.drawmapboundary()

    # draw and label ra/dec grid lines every 30 degrees.
    degtoralabel = lambda deg : "$%d^h$" % int(deg/15)
    degtodeclabel = lambda deg : "%+d$^\circ$" % deg
    map.drawparallels(np.arange(-90, 90, 30), color='lightgray')#, fmt=degtodeclabel)
    map.drawmeridians(np.arange(0, 360, 45), color='lightgray')

    for h in [0,3,6,9,12,15,18,21]:
        try:
            x,y = map(h*15,0)
            if abs(x) < 1e10 and abs(y) < 1e10:
                ax.text(x,y, degtoralabel(h*15), color='white')
        except:
            pass

    # Place stars
    #star_size = [10*(10**(-0.4*v)) for v in tycho2['v']]
    #px, py = map(tycho2['ra'], tycho2['dec'])
    #map.scatter(px, py, s=star_size, c='gray')

    # Counts
    if normalize:
        m = map.pcolormesh(ra0, dec0, counts_norm, latlon=True, cmap='hot', vmin=vmin, vmax=vmax)
    else:
        m = map.pcolormesh(ra0, dec0, counts, latlon=True, cmap='hot')
    map.colorbar(m)

    # Showers
    if night != 'all':
        showers = favor2.query("SELECT * FROM meteors_showers WHERE activity='annual'")
        solar_lon = get_solar_longitude(night_to_time(night))
        for shower in showers:
            delta = angle_delta(solar_lon, shower['solar_lon'])
            if delta < 3:
                px, py = map(shower['ra'], shower['dec'])
                #map.plot(px, py, marker='+', color='blue', markersize=10, markeredgewidth=2)
                color = {10:'orange'}.get(shower['status'], 'blue')
                ax.text(px, py, "%s" % (shower['iaucode']), color=color, va='center', ha='center')

    # Title etc
    ax.set_title("%d meteors, %d unique with directions" % (Nobj_total, Nobj))

    fig.suptitle("Meteors of night %s: radiants with %g degrees bin" % (night, binsize))

    # Return the image
    canvas = FigureCanvas(fig)
    canvas.print_png(file)#, bbox_inches='tight')

def meteors_map_plot(file, id=0, ra0=0, dec0=0, favor2=None, night='all', size=800, msize=2.0):
    if not favor2:
        favor2 = Favor2()

    if id:
        obj0 = favor2.query("SELECT * FROM meteors_view WHERE id=%s", (id,), simplify=True)
        ra0, dec0 = obj0['ra_start'], obj0['dec_start']
        night = obj0['night']

    objects = favor2.query("SELECT * FROM meteors_view WHERE ((night=%s OR %s=%s) AND (filter='Clear' OR filter='R')) OR (id=%s) ORDER BY time", (night, night, 'all', id), simplify=False)
    Nobj_total = len(objects)
    Nobj = 0

    dots0 = []
    dots2 = []
    for obj in objects:
        xyz1_1 = radectoxyz(obj['ra_start'], obj['dec_start'])
        xyz1_2 = radectoxyz(obj['ra_end'], obj['dec_end'])
        norm1 = np.cross(xyz1_1, xyz1_2)
        norm1 = norm1/np.linalg.norm(norm1)

        xyz0_1 = radectoxyz(ra0, dec0)
        xyz0_2 = radectoxyz(obj['ra_end'], obj['dec_end'])
        norm0 = xyz0_1
        norm0 = norm0/np.linalg.norm(norm0)
        norm2 = np.cross(xyz0_1, xyz0_2)
        norm2 = norm2/np.linalg.norm(norm2)

        dots0.append(np.dot(norm0, norm1))
        dots2.append(np.dot(norm2, norm1))

        pass

    # Plot the map of intersections
    # FIXME: it gives some strange errors with colorbar, let's use pyplot instead
    #fig = Figure(facecolor='white', dpi=72, figsize=(size/72, 0.6*size/72), tight_layout=True)
    fig = plt.figure(facecolor='white', dpi=72, figsize=(size/72, 0.8*size/72), tight_layout=False)
    ax = fig.add_subplot(111)

    # set up gnomonic map projection
    map = Basemap(projection='gnom', lat_0 = dec0, lon_0 = -ra0, resolution = None, celestial = True, rsphere = 1, width=msize, height=msize, ax = ax)
    # draw the edge of the map projection region (the projection limb)
    map.drawmapboundary()

    # draw and label ra/dec grid lines every 30 degrees.
    degtoralabel = lambda deg : "$%d^h$" % int(deg/15)
    degtodeclabel = lambda deg : "%+d$^\circ$" % deg
    map.drawparallels(np.arange(-90, 90, 30), color='lightgray', labels=[True, False, False, False])#, fmt=degtodeclabel)
    map.drawmeridians(np.arange(0, 360, 45), color='lightgray', labels=[False, True, False, True], fmt=degtoralabel)

    # Place stars
    star_size = [10*(10**(-0.4*v)) for v in tycho2['v']]
    px, py = map(tycho2['ra'], tycho2['dec'])
    map.scatter(px, py, s=star_size, c='gray')

    # Meteor
    for i,obj in enumerate(objects):
        px, py = map([obj['ra_start'], obj['ra_end']], [obj['dec_start'], obj['dec_end']])
        if abs(px[0])>3 or abs(py[0])>3:
            continue

        if id:
            if obj == obj0:
                alpha = 1.0
                #map.drawgreatcircle(obj['ra_start'], obj['dec_start'], obj['ra_end'], obj['dec_end'], 0.01)
            else:
                alpha = 0.1
        elif dots2[i] < 0:
            alpha = 0.1
        else:
            alpha = 0.1 + 0.9*((90-np.abs(np.rad2deg(np.arcsin(dots0[i]))))/90.0)**10

        ax.arrow(px[0], py[0], px[1]-px[0], py[1]-py[0], lw=0, head_width=0.01, shape='full',length_includes_head=False, fill=True, color='black', clip_on=True, alpha=alpha)

    # Showers
    showers = favor2.query("SELECT * FROM meteors_showers WHERE activity='annual'")
    solar_lon = get_solar_longitude(night_to_time(night))
    for shower in showers:
        delta = angle_delta(solar_lon, shower['solar_lon'])
        if delta < 3:
            px, py = map(shower['ra'], shower['dec'])
            if abs(px)<1e10 and abs(py) < 1e10:
                #map.plot(px, py, marker='+', color='blue', markersize=10, markeredgewidth=2)
                color = {10:'orange'}.get(shower['status'], 'blue')
                ax.text(px, py, "%s" % (shower['iaucode']), color=color, va='center', ha='center', clip_on=True)

    # Title etc
    if id:
        ax.set_title("Night %s, id=%d, ra0=%.1f, dec0=%.1f" % (night, id, ra0, dec0))
    else:
        ax.set_title("Night %s, ra0=%.1f, dec0=%.1f" % (night, ra0, dec0))

    # Return the image
    canvas = FigureCanvas(fig)
    canvas.print_png(file, bbox_inches='tight')

if __name__ == '__main__':
    f = Favor2()

    meteors_radiants_plot(file='out.png', night='2014_10_11', normalize=True)
