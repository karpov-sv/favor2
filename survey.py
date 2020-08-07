#!/usr/bin/env python

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
import StringIO, base64

from matplotlib.patches import Ellipse
import numpy as np
import posixpath, glob, datetime, os, sys, tempfile, shutil

from astropy import wcs as pywcs
from astropy.io import fits as pyfits
from astropy.coordinates import SkyCoord

import secondscale as ss

from astroquery.simbad import Simbad
from astroquery.vizier import Vizier
from esutil import htm, coords
import ephem
from coord import ang_sep

import urllib, requests
import astropy.time
import astropy.io.votable

import cv2

import statsmodels.api as sm
from scipy.spatial import cKDTree
from scipy.special import legendre as leg

from favor2 import Favor2, fix_remote_path

def get_frame_center(filename=None, header=None, wcs=None, width=None, height=None):
    if not wcs:
        if header:
            wcs = pywcs.WCS(header=header)
        elif filename:
            header = pyfits.getheader(filename, -1)
            wcs = pywcs.WCS(header=header)

    if (not width or not height) and header:
        width = header['NAXIS1']
        height = header['NAXIS2']

    [ra1],[dec1] = wcs.all_pix2world([0], [0], 1)
    [ra0],[dec0] = wcs.all_pix2world([width/2], [height/2], 1)

    sr = coords.sphdist(ra0, dec0, ra1, dec1)[0]

    return ra0, dec0, sr

def crop_image(image0, x0, y0, size=200):
    h, w = image0.shape

    x0i = int(round(x0))
    y0i = int(round(y0))

    x1 = max(0, min(x0i - size, w - 1))
    y1 = max(0, min(y0i - size, h - 1))
    x2 = min(w - 1, max(x0i + size, 0))
    y2 = min(h - 1, max(y0i + size, 0))

    return image0[y1:y2, x1:x2], x1, y1

def filter_cosmics(image, threshold=5.0):
    dx = [-1, -1, -1, 0, 0, 1, 1, 1]
    dy = [-1, 0, 1, -1, 1, -1, 0, 1]

    imgs = [np.roll(np.roll(image, dx[i], 1), dy[i], 0) for i in xrange(len(dx))]
    median = np.median(imgs, axis=0)
    std = np.std(imgs, axis=0)

    idx = np.abs(image - median) > threshold*std

    result = image.copy()
    result[idx] = median[idx]

    return result

def filter_cosmics_in_file(filename, outname, threshold=5.0):
    image = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    image = filter_cosmics(image, threshold)

    pyfits.writeto(outname, image, header, clobber=True)

def check_gsc23(ra, dec, radius="30 seconds", limit=15):
    try:
        res = Vizier.query_region(SkyCoord(ra, dec, unit='deg'), radius="30 seconds", catalog="I/305/out")
        result = False

        if res:
            for filter in ['Fmag', 'Vmag']:
                mags = [r[filter] for r in res[0] if r[filter] > 0]

                if mags:
                    magsum = -2.5*np.log10(np.sum([10**(-0.4*m) for m in mags]))

                    if magsum < limit:
                        result = True
                        break

        return result
    except:
        return True

def check_2mass(ra, dec, radius="30 seconds", limit=14):
    try:
        res = Vizier.query_region(SkyCoord(ra, dec, unit='deg'), radius="30 seconds", catalog="II/246/out")
        result = False

        if res:
            for filter in ['Jmag']:
                mags = [r[filter] for r in res[0] if r[filter] > 0]

                if mags:
                    magsum = -2.5*np.log10(np.sum([10**(-0.4*m) for m in mags]))

                    if magsum < limit:
                        result = True
                        break

        return result
    except:
        return True

def check_tycho2(ra, dec, radius=30.0/3600, limit=12, favor2=None):
    favor2 = favor2 or Favor2()
    res = favor2.get_stars(ra, dec, radius, catalog='tycho2', extra="vt < %g" % limit)
    if len(res):
        return True
    else:
        return False


def check_planets(ra, dec, time, favor2=None):
    favor2 = favor2 or Favor2()
    for o in [ephem.Moon(), ephem.Venus(), ephem.Mars(), ephem.Jupiter()]:
        favor2.obs.date = time
        o.compute(favor2.obs)

        sr = {'Moon': 15.0,
              'Venus': 0.5,
              'Jupiter': 0.5,
              'Mars': 0.1}.get(o.name)

        if coords.sphdist(np.rad2deg(o.ra), np.rad2deg(o.dec), ra, dec)[0] < sr:
            return True

    return False

def filter_indices(uidx, obj, time, width=10):
    mask = np.zeros([10 + int(np.max(obj['y'])), 10 + int(np.max(obj['x']))])

    for i in uidx:
        x, y = int(np.round(obj['x'][i])), int(np.round(obj['y'][i]))
        mask[y, x] = 1

    lines = cv2.HoughLines(mask.astype(np.uint8), 1, np.pi*0.5/180, 4)

    rho, theta = (0, 0)

    if lines is not None:
        for line in lines[0]:
            if line[0] or line[1]:
                rho, theta = line
                break

    result = []

    for i in uidx:
        b = rho - obj['x'][i]*np.cos(theta) - obj['y'][i]*np.sin(theta)
        is_good = True

        if np.abs(b) < width:
            is_good = False

        if check_planets(obj['ra'][i], obj['dec'][i], time) or check_tycho2(obj['ra'][i], obj['dec'][i], 0.05, limit=8) or check_tycho2(obj['ra'][i], obj['dec'][i], 0.08, limit=7) or check_tycho2(obj['ra'][i], obj['dec'][i], 0.15, limit=6) or check_gsc23(obj['ra'][i], obj['dec'][i]) or check_2mass(obj['ra'][i], obj['dec'][i]):
            is_good = False

        if is_good:
            result.append(i)

    return result

def get_skybot(ra, dec, sr, time, objFilter=111, maglim=20.0):
    epoch = astropy.time.Time(time).jd
    params = urllib.urlencode({'EPOCH': epoch,
                               'RA': ra,
                               'DEC': dec,
                               'SR': max(sr, 300.0/3600),
                               'VERB': 3,
                               '-objFilter': objFilter,
                               '-loc': 'C32',
    })
    url = 'http://vo.imcce.fr/webservices/skybot/conesearch'

    try:
        while True:
            srccat = requests.get(url, params).text
            s = StringIO.StringIO(srccat.replace('width="*"', 'width="100"'))
            vots = astropy.io.votable.parse(s)

            is_ok = False
            for i in vots.infos:
                if i.ID == 'status' and i.value == 'OK':
                    is_ok = True

            if is_ok:
                break

            sleep(2)

        if vots.groups:
            arr = vots.get_first_table().array
            arr = arr[(arr['d'] < sr*3600) & (arr['Mv'] < maglim)] # [a for a in arr if a['d'] < sr and a['Mv'] < maglim]
            if not len(arr):
                return None
            else:
                return arr

    except:
        pass

    return None

def find_transients_in_frame(filename, refine=True, favor2=None, workdir=None, figure=None, show=False, verbose=False, plot=True):
    transients = []

    favor2 = favor2 or Favor2()

    ra0, dec0, sr0 = get_frame_center(filename=filename)
    print "Frame %s at RA=%g Dec=%g with SR=%g" % (filename, ra0, dec0, sr0)

    # Dir to hold all temporary files
    basedir = workdir or tempfile.mkdtemp(prefix='survey')
    filtered_name = posixpath.join(basedir, "filtered.fits")

    print "Cleaning the frame"
    cleaned_name = posixpath.join(basedir, "clean.fits")
    filter_cosmics_in_file(filename, cleaned_name)

    header = pyfits.getheader(cleaned_name, -1)
    time = datetime.datetime.strptime(header['TIME'], '%d.%m.%Y %H:%M:%S.%f')

    print "Pre-processing the frame"
    os.system("nebuliser %s noconf %s 3 3 --takeout_sky" % (cleaned_name, filtered_name))

    if refine:
        print "Refining the WCS"
        matched_name = posixpath.join(basedir, "image.fits")
        if not favor2.blind_match_file(filtered_name, outfile=matched_name):
            print "Failure refining the WCS"
            return transients
    else:
        matched_name = filtered_name

    print "Extracting the objects"
    obj = ss.get_objects(filename=matched_name, options={'DETECT_MINAREA':3.0, 'DETECT_THRESH':1.5, 'BACKPHOTO_TYPE':'GLOBAL', 'DEBLEND_NTHRESH':64, 'DEBLEND_MINCONT':0.001, 'FILTER':'Y', 'BACK_TYPE':'MANUAL', 'BACK_VALUE': 0.0, 'BACK_SIZE':64, 'BACK_FILTERSIZE':3}, fwhm=2, aper_fwhm=2)

    print "Querying the catalogues"
    cat = favor2.get_stars(ra0, dec0, sr0, catalog='twomass', limit=1000000)
    catn = favor2.get_stars(ra0, dec0, sr0, catalog='nomad', limit=1000000)
    catt = favor2.get_stars(ra0, dec0, sr0, catalog='tycho2', limit=1000000)
    cattb = favor2.get_stars(ra0, dec0, sr0, catalog='tycho2', limit=1000000, extra='vt < 7')

    print "%d objects, %d 2MASS stars, %d NOMAD stars, %d bright Tycho2 stars" % (len(obj['ra']), len(cat['ra']), len(catn['ra']), len(cattb['ra']))

    print "Matching the objects"
    wcs = pywcs.WCS(header)
    pixscale = wcs.pixel_scale_matrix.max()*3600 # estimate of pixel scale, in arcsec/pix
    size = 2.0*obj['A_WORLD']*pixscale
    size = np.maximum(size, np.repeat(40.0, len(obj['ra'])))

    h = htm.HTM(10)
    m = h.match(obj['ra'], obj['dec'], cat['ra'], cat['dec'], size/3600, maxmatch=0)
    mn = h.match(obj['ra'], obj['dec'], catn['ra'], catn['dec'], size/3600, maxmatch=0)
    mt = h.match(obj['ra'], obj['dec'], catt['ra'], catt['dec'], size/3600, maxmatch=0)
    if len(cattb['ra']):
        mtb = h.match(obj['ra'], obj['dec'], cattb['ra'], cattb['dec'], (size + 100.0)/3600, maxmatch=0)

    uidx=np.setdiff1d(np.arange(len(obj['ra'])), m[0])
    print "%d objects not matched with 2MASS" % len(uidx)

    uidx=np.setdiff1d(uidx, mn[0])
    print "%d objects not matched with NOMAD" % len(uidx)

    uidx=np.setdiff1d(uidx, mt[0])
    print "%d objects not matched with Tycho2" % len(uidx)

    if len(cattb['ra']):
        uidx=np.setdiff1d(uidx, mtb[0])
    print "%d objects not matched with brightest Tycho2 stars" % len(uidx)

    if len(uidx) < 100:
        uidx = filter_indices(uidx, obj, time)
        print "%d objects after line filtering and GSC 2.3.2 matching" % len(uidx)

    # Get the image and project the catalogues onto it
    image = pyfits.getdata(filtered_name, -1)

    cat_x, cat_y = wcs.all_world2pix(cat['ra'], cat['dec'], 0)
    catn_x, catn_y = wcs.all_world2pix(catn['ra'], catn['dec'], 0)
    catt_x, catt_y = wcs.all_world2pix(catt['ra'], catt['dec'], 0)
    if len(cattb['ra']):
        cattb_x, cattb_y = wcs.all_world2pix(cattb['ra'], cattb['dec'], 0)

    if len(uidx) > 100:
        print "Too many unmatched objects (%d), something is wrong!" % len(uidx)
    else:
        # Let's iterate over the transients
        for i in uidx:
            transient = {'x': float(obj['x'][i]),
                         'y': float(obj['y'][i]),
                         'flux': float(obj['FLUX_AUTO'][i]),
                         'flux_err': float(obj['FLUXERR_AUTO'][i]),
                         'flags': int(obj['FLAGS'][i]),
                         'a': float(obj['A_WORLD'][i]),
                         'b': float(obj['B_WORLD'][i]),
                         'theta': float(obj['THETA_WORLD'][i]),
                         'ra': float(obj['ra'][i]),
                         'dec': float(obj['dec'][i]),
                         'filename': filename,
                         'time': time,
                         'channel_id': header['CHANNEL ID']
                        }

            transient['simbad'] = None
            transient['mpc'] = None

            try:
                res = Simbad.query_region(SkyCoord(obj['ra'][i], obj['dec'][i], unit='deg'), "30 seconds")
                if res is not None:
                    transient['simbad'] = ", ".join([r for r in res['MAIN_ID']])
            except:
                pass

            try:
                # Minor planets
                mpc = get_skybot(obj['ra'][i], obj['dec'][i], 30.0/3600, time, objFilter=110)

                # Comets
                if mpc is None:
                    mpc = get_skybot(obj['ra'][i], obj['dec'][i], 300.0/3600, time, objFilter=1)

                if mpc is not None:
                    transient['mpc'] = ", ".join(["%s %s %s (Mv=%s)" % (mpc['class'][ii], mpc['num'][ii], mpc['name'][ii], mpc['Mv'][ii]) for ii in xrange(len(mpc['name']))])
            except:
                pass

            if verbose:
                #print transient
                print "x = %g y = %g" % (obj['x'][i], obj['y'][i])
                print "RA/Dec = %g %g = %s %s" % (obj['ra'][i], obj['dec'][i], str(ephem.hours(obj['ra'][i]*np.pi/180)).replace(":", " "), str(ephem.degrees(obj['dec'][i]*np.pi/180)).replace(":"," "))
                time0 = time.replace(hour=0, minute=0, second=0, microsecond=0)
                print "Time = %s" % time
                print "Time = %s + %g" % (time0, (time-time0).total_seconds()/24/3600)

                if transient['simbad']:
                    print "SIMBAD: " + transient['simbad']

                print

            if plot:
                icrop,x0,y0 = crop_image(image, obj['x'][i], obj['y'][i], 40)
                limits=np.percentile(icrop, [0.5, 99.5])

                if not figure:
                    size=400
                    figure = Figure(facecolor='white', dpi=72, figsize=(size/72, size/72), tight_layout=True)

                figure.clear()

                ax = figure.add_axes([0,0,1,1])
                ax.axis('off')

                im = ax.imshow(icrop, origin="bottom", interpolation="nearest", cmap="hot", vmin=limits[0], vmax=limits[1])
                #figure.colorbar(im)
                ax.autoscale(False)
                ax.scatter(obj['x']-x0, obj['y']-y0, marker='o', color='green', s=400, facecolors='None', lw=3)
                ax.scatter(cat_x-x0, cat_y-y0, marker='o', color='blue', s=100, facecolors='None', lw=2)
                ax.scatter(catt_x-x0, catt_y-y0, marker='x', s=100, color='blue', facecolors='None', lw=2)

                #ellipse = Ellipse(xy=(obj['x'][i]-x0, obj['y'][i]-y0), width=2.0*obj['A_WORLD'][i], height=2.0*obj['B_WORLD'][i], angle=obj['THETA_WORLD'][i], edgecolor='b', fc='None', axes=ax)
                #ax.add_patch(ellipse)

                if show:
                    figure.show(warn=False)
                else:
                    canvas = FigureCanvas(figure)
                    sio = StringIO.StringIO()
                    canvas.print_png(sio, bbox_inches='tight')

                    transient['preview'] = "data:image/png;base64," + base64.b64encode(sio.getvalue())
                    pass
            else:
                transient['preview'] = None

            transients.append(transient)

    # cleanup
    if not workdir:
        shutil.rmtree(basedir)

    print "Done"

    return transients

def find_transients(night='all', type='survey', id=0, reprocess=False, base='.', channel_id=0, verbose=False):
    favor2 = Favor2()

    where = [];
    where_opts = [];

    if not reprocess:
        where.append("keywords->'transients_extracted' != '1'")
    if id:
        where.append("id = %s")
        where_opts.append(int(id))
    if type:
        where.append("type = %s")
        where_opts.append(type)
    if night and night != 'all':
        where.append("night = %s")
        where_opts.append(night)
    if channel_id:
        where.append("channel_id = %s")
        where_opts.append(channel_id)

    if where:
        where_string = "WHERE " + " AND ".join(where)

    res = favor2.query("SELECT * FROM images %s ORDER BY time;" % (where_string), where_opts, simplify=False, debug=True)

    for r in res:
        filename = r['filename']
        filename = fix_remote_path(filename, channel_id=r['channel_id'])
        transients = find_transients_in_frame(filename, verbose=verbose, favor2=favor2)

        for transient in transients:
            favor2.query("INSERT INTO survey_transients (channel_id, frame_id)")

        favor2.query("UPDATE images SET keywords = keywords || 'transients_extracted=>1' WHERE id=%s", (r['id'],))

### New survey photometry code

def get_objects_from_file(filename, wcs=None, simple=False, xlim=None, ylim=None, filter=True, danger=False, sex=False):
    if simple:
        catname = filename + '.simple'
    else:
        catname = filename + '.txt'

    if not posixpath.exists(catname) and posixpath.exists(filename) and not 'fits' in filename.lower():
        catname = filename

    if not posixpath.exists(catname):
        return None

    if not wcs and ('fits' in filename.lower() or 'fit' in filename.lower() or 'fts' in filename.lower()):
        header = pyfits.getheader(filename, -1)
        wcs = pywcs.WCS(header)
    else:
        header = None

    with open(catname, 'r') as file:
        lines = file.readlines()
        x,y,flux,fluxerr,flags,a,b,theta,chisq,bg,id = [],[],[],[],[],[],[],[],[],[],[]

        if not lines:
            return None

        for i in xrange(len(lines)):
            s = [float(ss) for ss in lines[i].split()]

            if xlim and (s[0] < xlim[0] or s[0] > xlim[1]):
                continue
            if ylim and (s[1] < ylim[0] or s[1] > ylim[1]):
                continue
            #if s[2] <= 0:
            #    continue

            s[4] = int(s[4])
            if not danger:
                s[4] = s[4] & ~0x20

            if filter and s[4] > 0:
                continue

            if filter and s[2] <= 0:
                continue

            if header:
                if s[0] < 2 or s[0] >= header['NAXIS1'] - 2 or s[1] < 2 or s[1] >= header['NAXIS2'] - 2:
                    continue

            x.append(s[0])
            y.append(s[1])
            flux.append(s[2])
            fluxerr.append(s[3])
            flags.append(int(s[4]))

            a.append(s[5])
            b.append(s[6])
            theta.append(s[7])
            bg.append(s[8])
            chisq.append(s[9])
            id.append(s[10])

        x,y,flux,fluxerr,flags,a,b,theta,chisq,bg,id = [np.array(_) for _ in x,y,flux,fluxerr,flags,a,b,theta,chisq,bg,id]
        #x,y,flux,fluxerr,flags,chisq,bg,fwhm,ellipticity,minflux,maxflux = [np.array(t) for t in x,y,flux,fluxerr,flags,chisq,bg,fwhm,ellipticity,minflux,maxflux]

        if sex:
            x -= 1
            y -= 1

        obj = {}
        obj['x'], obj['y'] = x, y
        obj['ra'], obj['dec'] = wcs.all_pix2world(x, y, 0) if wcs else None,None
        obj['flux'] = flux
        obj['fluxerr'] = fluxerr
        obj['flags'] = flags
        obj['chisq'] = chisq
        obj['bg'] = bg
        obj['mag'] = -2.5*np.log10(obj['flux'])
        obj['magerr'] = 2.5*fluxerr/flux/np.log(10) # 2.5*np.log10(1 + fluxerr/flux)

        obj['a'] = a
        obj['b'] = b
        obj['theta'] = theta
        obj['id'] = id

        # obj['fwhm'] = fwhm
        # obj['ellipticity'] = ellipticity
        # obj['minflux'] = minflux
        # obj['maxflux'] = maxflux

    return obj

def fix_distortion(obj, cat, header=None, wcs=None, width=None, height=None, dr=3.0):
    if header:
        wcs = pywcs.WCS(header)
        width = header['NAXIS1']
        height = header['NAXIS2']

    if not wcs or not width or not height:
        print "Nothing to fix"
        return

    kdo = cKDTree(np.array([obj['x'], obj['y']]).T)
    xc,yc = wcs.all_world2pix(cat['ra'], cat['dec'], 0)
    kdc = cKDTree(np.array([xc,yc]).T)

    m = kdo.query_ball_tree(kdc, dr)
    nm = np.array([len(_) for _ in m])

    # Distortions
    dx = np.array([obj['x'][_] - xc[m[_][0]] if nm[_] == 1 else 0 for _ in xrange(len(m))])
    dy = np.array([obj['y'][_] - yc[m[_][0]] if nm[_] == 1 else 0 for _ in xrange(len(m))])

    # Normalized coordinates
    x = (obj['x'] - width/2)*2.0/width
    y = (obj['y'] - height/2)*2.0/height

    X = [np.ones_like(x), x, y, x*x, x*y, y*y, x*x*x, x*x*y, x*y*y, y*y*y, x*x*x*x, x*x*x*y, x*x*y*y, x*y*y*y, y*y*y*y, x*x*x*x*x, x*x*x*x*y, x*x*x*y*y, x*x*y*y*y, x*y*y*y*y, y*y*y*y*y, x*x*x*x*x*x, x*x*x*x*x*y, x*x*x*x*y*y, x*x*x*y*y*y, x*x*y*y*y*y, x*y*y*y*y*y, y*y*y*y*y*y]
    X = np.vstack(X).T
    Yx = dx
    Yy = dy

    # Use only unique matches
    idx = (nm == 1) # & (distm < 2.0*np.std(distm))
    Cx = sm.WLS(Yx[idx], X[idx]).fit()
    Cy = sm.WLS(Yy[idx], X[idx]).fit()

    YYx = np.sum(X*Cx.params, axis=1)
    YYy = np.sum(X*Cy.params, axis=1)

    obj['ra'],obj['dec'] = wcs.all_pix2world(obj['x']-YYx, obj['y']-YYy, 0)

def make_series(mul=1.0, x=1.0, y=1.0, order=1, sum=False, legendre=False, zero=True):
    if zero:
        res = [np.ones_like(x)*mul]
    else:
        res = []

    for i in xrange(1,order+1):
        maxr = i+1
        if legendre:
            maxr = order+1

        for j in xrange(maxr):
            #print i, '-', i - j, j
            if legendre:
                res.append(mul * leg(i)(x) * leg(j)(y))
            else:
                res.append(mul * x**(i-j) * y**j)
    if sum:
        return np.sum(res, axis=0)
    else:
        return res

def radectoxyz(ra_deg, dec_deg):
    ra  = np.deg2rad(ra_deg)
    dec = np.deg2rad(dec_deg)
    xyz = np.array((np.cos(dec)*np.cos(ra),
                    np.cos(dec)*np.sin(ra),
                    np.sin(dec)))

    return xyz

def xyztoradec(xyz):
    ra = np.arctan2(xyz[1], xyz[0])
    ra += 2*np.pi * (ra < 0)
    dec = np.arcsin(xyz[2] / np.linalg.norm(xyz, axis=0))

    return (np.rad2deg(ra), np.rad2deg(dec))


def calibrate_objects(obj, cat, sr=15.0/3600, smooth=False, correct_brightness=False, smoothr=100, smoothmin=20, retval='std'):
    h = htm.HTM(10)
    m = h.match(obj['ra'], obj['dec'], cat['ra'], cat['dec'], sr, maxmatch=0)

    oidx = m[0]
    cidx = m[1]
    dist = m[2]*3600

    if len(oidx) < 300:
        return None

    x,y = obj['x'][oidx],obj['y'][oidx]

    x = (x - 2560/2)*2/2560
    y = (y - 2160/2)*2/2160

    omag = obj['mag'][oidx]
    omagerr = obj['magerr'][oidx]
    oflux = obj['flux'][oidx]
    ofluxerr = obj['fluxerr'][oidx]
    oflags = obj['flags'][oidx]
    ochisq = obj['chisq'][oidx]

    omag0 = (omag-np.min(omag))/(np.max(omag)-np.min(omag))

    try:
        cb = cat['g'][cidx]
        cv = cat['r'][cidx]
        cr = cat['i'][cidx]
        cmagerr = np.hypot(cat['gerr'][cidx], cat['rerr'][cidx], cat['ierr'][cidx])
    except:
        cb = cat['b'][cidx]
        cv = cat['v'][cidx]
        cr = cat['r'][cidx]

        try:
            cmagerr = np.hypot(cat['berr'][cidx], cat['verr'][cidx], cat['rerr'][cidx])
        except:
            cmagerr = np.hypot(cat['ebt'][cidx], cat['evt'][cidx])

    cmag = cv
    tmagerr = np.hypot(cmagerr, omagerr)

    delta_mag = cmag - omag
    weights = 1.0/tmagerr

    X = [np.ones(len(delta_mag)), x, y, x*x, x*y, y*y, x*x*x, x*x*y, x*y*y, y*y*y, x*x*x*x, x*x*x*y, x*x*y*y, x*y*y*y, y*y*y*y, x*x*x*x*x, x*x*x*x*y, x*x*x*y*y, x*x*y*y*y, x*y*y*y*y, y*y*y*y*y, x*x*x*x*x*x, x*x*x*x*x*y, x*x*x*x*y*y, x*x*x*y*y*y, x*x*y*y*y*y, x*y*y*y*y*y, y*y*y*y*y*y]
    #X += [omag0, omag0**2, omag0**3, omag0**4, omag0**5, omag0**6]
    X += [cb-cv, (cb-cv)*x, (cb-cv)*y, (cb-cv)*x*x, (cb-cv)*x*y, (cb-cv)*y*y, (cb-cv)*x*x*x, (cb-cv)*x*x*y, (cb-cv)*x*y*y, (cb-cv)*y*y*y]
    X += [cv-cr, (cv-cr)*x, (cv-cr)*y, (cv-cr)*x*x, (cv-cr)*x*y, (cv-cr)*y*y, (cv-cr)*x*x*x, (cv-cr)*x*x*y, (cv-cr)*x*y*y, (cv-cr)*y*y*y]

    X = np.vstack(X).T
    Y = delta_mag

    idx = (oflags == 0) & (ochisq < 2.0)

    for iter in range(5):
        if len(X[idx]) < 100:
            print "Fit failed - %d objects" % len(X[idx])
            return None

        C = sm.WLS(Y[idx], X[idx], weights=weights[idx]).fit()
        YY = np.sum(X*C.params,axis=1)
        idx = (np.abs(Y-YY) < 2.0*np.std(Y-YY)) & (oflags==0) & (ochisq < 2.0)

        print np.std((Y-YY)[idx]), np.std(Y-YY), '-', np.std(((Y-YY)/tmagerr)), np.std((((Y-YY)/tmagerr))[idx]),'-',len(idx[idx])

    mag0 = YY
    mag = omag + mag0

    x = (obj['x'] - 2560/2)*2/2560
    y = (obj['y'] - 2160/2)*2/2160

    omag0 = (obj['mag']-np.min(omag))/(np.max(omag)-np.min(omag))

    obj['mag0'] = C.params[0]*np.ones_like(obj['mag']) + C.params[1]*x + C.params[2]*y + C.params[3]*x*x + C.params[4]*x*y + C.params[5]*y*y + C.params[6]*x*x*x + C.params[7]*x*x*y + C.params[8]*x*y*y + C.params[9]*y*y*y + C.params[10]*x*x*x*x + C.params[11]*x*x*x*y + C.params[12]*x*x*y*y + C.params[13]*x*y*y*y + C.params[14]*y*y*y*y + C.params[15]*x*x*x*x*x + C.params[16]*x*x*x*x*y + C.params[17]*x*x*x*y*y + C.params[18]*x*x*y*y*y + C.params[19]*x*y*y*y*y + C.params[20]*y*y*y*y*y + C.params[21]*x*x*x*x*x*x + C.params[22]*x*x*x*x*x*y + C.params[23]*x*x*x*x*y*y + C.params[24]*x*x*x*y*y*y + C.params[25]*x*x*y*y*y*y + C.params[26]*x*y*y*y*y*y + C.params[27]*y*y*y*y*y*y
    #obj['mag0'] += C.params[28]*omag0 + C.params[29]*omag0**2 + C.params[30]*omag0**3 + C.params[31]*omag0**4 + C.params[32]*omag0**5 + C.params[33]*omag0**6

    obj['cmag'] = obj['mag'] + obj['mag0']

    obj['Cbv'] = np.ones_like(obj['mag'])*C.params[-20] + x*C.params[-19] + y*C.params[-18] + x*x*C.params[-17] + x*y*C.params[-16] + y*y*C.params[-15] + x*x*x*C.params[-14] + x*x*y*C.params[-13] + x*y*y*C.params[-12] + y*y*y*C.params[-11]
    obj['Cvr'] = np.ones_like(obj['mag'])*C.params[-10] + x*C.params[-9] + y*C.params[-8] + x*x*C.params[-7] + x*y*C.params[-6] + y*y*C.params[-5] + x*x*x*C.params[-4] + x*x*y*C.params[-3] + x*y*y*C.params[-2] + y*y*y*C.params[-1]

    # Correct uncorrected brightness-dependent variations
    obj['bcorr'] = np.zeros_like(obj['cmag'])

    if correct_brightness:
        step = 0.5
        omag0 = omag + np.median(cmag-omag)

        for vmin in np.arange(5, 16, step):
            idx0 = (omag0[idx] >= vmin) & (omag0[idx] < vmin + step)
            if len(omag0[idx][idx0] > 20):
                omag1 = obj['mag'] + np.median(cmag-omag)
                idx1 = (omag1 >= vmin) & (omag1 < vmin + step)
                obj['bcorr'][idx1] = np.average((Y-YY)[idx][idx0], weights=1.0/tmagerr[idx][idx0])

    # Local smoothing of residuals
    if smooth:
        kd0 = cKDTree(np.array([obj['x'][oidx][idx],obj['y'][oidx][idx]]).T)
        kd = cKDTree(np.array([obj['x'],obj['y']]).T)

        m = kd.query_ball_tree(kd0, smoothr)
        nm = np.array(([len(_) for _ in m]))

        corr = np.array([np.average((Y-YY)[idx][_], weights=weights[idx][_]) if len(_) > smoothmin else 0 for _ in m])
        icorr = nm > smoothmin

        obj['corr'] = corr
        obj['corrected'] = icorr
        obj['ncorr'] = nm
    else:
        obj['corr'] = np.zeros_like(obj['mag'])
        obj['corrected'] = np.ones_like(obj['mag'], dtype=np.bool)
        obj['ncorr'] = np.zeros_like(obj['mag'])

    if retval == 'std':
        return np.std(Y-YY)
    elif retval == 'map':
        return Y
    elif retval == 'fit':
        return YY
    elif retval == 'diff':
        return Y-YY
    elif retval == 'all':
        return Y, YY, obj['corr'], oidx

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-b', '--base', help='base path, default to ./', action='store', dest='base', default='')
    parser.add_option('-r', '--reprocess', help='Process all images, even already processed', action='store_true', dest='reprocess', default=False)
    parser.add_option('-n', '--night', help='Night to process, defaults to all', action='store', dest='night', default='')
    parser.add_option('-i', '--id', help='Image ID, default to 0', action='store', dest='id', default=0)
    parser.add_option('-c', '--channel', help='Channel ID', action='store', dest='channel_id', default=0)
    parser.add_option('-f', '--file', help='File to process', action='store', dest='filename', default='')
    parser.add_option('-t', '--transients', help='Find transients', action='store_true', dest='find_transients', default=False)
    parser.add_option('-v', '--verbose', help='Verbose', action='store_true', dest='verbose', default=False)

    (options, args) = parser.parse_args()

    if options.filename:
        find_transients_in_frame(options.filename, verbose=options.verbose)
    else:
        find_transients(night=options.night, id=options.id, reprocess=options.reprocess, base=options.base, verbose=options.verbose, channel_id=options.channel_id)
