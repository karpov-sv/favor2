#!/usr/bin/env python

from astropy import wcs as pywcs
from astropy.io import fits as pyfits

import numpy as np
import os, shutil
import tempfile
import posixpath

import statsmodels.api as sm

from sextractor import Sextractor
from favor2 import Favor2

from esutil import coords, htm

def has_wcs(filename=None, header=None):
    if not header:
        header = pyfits.getheader(filename, -1)

    w = pywcs.WCS(header=header)

    return w.wcs.has_cd()

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

def merge_dicts(opt1, opt2={}):
    res = opt1.copy()
    res.update(opt2)

    return res

def get_objects(image=None, header=None, filename=None, wcs=None, aper_fwhm=4, fwhm=0, options={}, params=[]):
    if image is None:
        image = pyfits.getdata(filename, -1)
    if header is None and filename:
        header = pyfits.getheader(filename, -1)
    if wcs is None and header:
        wcs = pywcs.WCS(header=header)

    sex = Sextractor()

    if not fwhm:
        # Estimate the FWHM
        sex.run(image=image,
                params = ["MAG_AUTO", "FWHM_IMAGE", "FLAGS"],
                options = {'SATUR_LEVEL': 65534},
                parse=False)

        s0 = sex.table

        fwhms = s0['FWHM_IMAGE']
        idx = s0['FLAGS'] == 0

        fwhm = np.median(fwhms[idx])

    # Second run, using aperture with radius equal to FWHM
    sex.run(image=image,
            params = ["MAG_APER", "MAGERR_APER", "MAG_AUTO", "MAGERR_AUTO",
                      "X_IMAGE", "Y_IMAGE",
                      "A_IMAGE", "B_IMAGE", "THETA_IMAGE", "FLAGS", "ELLIPTICITY",
                      "FWHM_IMAGE", "BACKGROUND",
                      "FLUX_AUTO", "FLUXERR_AUTO", "FLUX_APER", "FLUXERR_APER",
                      "A_WORLD", "B_WORLD", "THETA_WORLD",
                      "X_WORLD", "Y_WORLD"] + params,
            options = merge_dicts({'BACKPHOTO_TYPE':'GLOBAL',
                                   'BACK_SIZE': 128,
                                   'BACK_FILTERSIZE': 3,
                                   'DETECT_MINAREA': 4,
                                   'DETECT_THRESH':3,
                                   'DEBLEND_MINCONT': 0.001,
                                   'SATUR_LEVEL': 65534,
                                   'PHOT_APERTURES': aper_fwhm*fwhm,
                                   # 'CHECKIMAGE_TYPE': 'BACKGROUND,BACKGROUND_RMS,FILTERED,OBJECTS,-OBJECTS',
                                   # 'CHECKIMAGE_NAME': 'out_bg.fits,out_bg_rms.fits,out_filtered.fits,out_objects.fits,out_mobjects.fits',
            }, options),
            parse=False)

    s0 = sex.table

    # Filter extracted objects
    idx = (s0['FLAGS'] == 0) | (s0['FLAGS'] == 1)
    idx = np.logical_or(idx, s0['FLAGS'] == 2)

    idx = np.logical_and(idx, np.isfinite(s0['FLUX_AUTO']))
    idx = np.logical_and(idx, np.isfinite(s0['FLUXERR_AUTO']))

    idx = np.logical_and(idx, s0['FLUX_AUTO'] > 0)
    idx = np.logical_and(idx, s0['FLUX_APER'] > 0)
    # idx = np.logical_and(idx, s0['FLUX_AUTO']/s0['FLUXERR_AUTO'] > 10)

    #idx = np.logical_and(idx, s0['ELLIPTICITY'] < 0.2)

    if False or image.shape == (1032,1392):
        # MMT-6, let's reject events on frame edges etc
        idx = np.logical_and(idx, s0['X_IMAGE'] < image.shape[1]-100)
        idx = np.logical_and(idx, s0['X_IMAGE'] > 100)
        idx = np.logical_and(idx, s0['Y_IMAGE'] < image.shape[0]-100)
        idx = np.logical_and(idx, s0['Y_IMAGE'] > 100)

    # We will return the dict of Numpy arrays
    s = {}
    for name in s0.names:
        s[name] = s0[idx][name]

    # Rename some columns for convenience
    s['mag'] = s['MAG_APER']
    #s['mag'] = s['MAG_AUTO']
    s['mag_err'] = s['MAGERR_APER']
    s['flux'] = s['FLUX_AUTO']
    s['flux_err'] = s['FLUXERR_AUTO']
    s['x'] = s['X_IMAGE']-1 # We want the origin of coordinates at zero, not one
    s['y'] = s['Y_IMAGE']-1

    s['ra'], s['dec'] = wcs.all_pix2world(s['x'], s['y'], 0)

    return s

def get_objects_fistar(image=None, header=None, filename=None):
    if image is None:
        image = pyfits.getdata(filename, -1)
        header = pyfits.getheader(filename, -1)

    dir = tempfile.mkdtemp('fistar')

    if not filename and image != None:
        filename = posixpath.join(dir, 'image.fits')
        pyfits.writeto(filename, image, clobber=True)

    catname = posixpath.join(dir, 'out.cat')

    if not filename:
        return None

    params = ['id', 'x', 'y', 'bg', 'd', 'k', 'fwhm', 's/n', 'flux', 'magnitude']
    command = 'fistar %s -o %s --format %s --mag-flux 0,1 -t 10' % (filename, catname, ','.join(params))
    os.system(command)

    s = {}

    with open(catname, 'r') as file:
        lines = file.readlines()
        N = len(lines)

        for iparam,param in enumerate(params):
            arr = np.empty(N)

            for i,line in enumerate(lines):
                sl = line.split()
                arr[i] = float(sl[iparam])

            s[param] = arr

    if dir != '.':
        #print "Cleaning dir %s" % dir
        shutil.rmtree(dir, ignore_errors=True)
        pass

    wcs = pywcs.WCS(header=header)

    s['ra'], s['dec'] = wcs.all_pix2world(s['x'], s['y'], 1)
    s['mag'] = s['magnitude']

    return s

def get_objects_from_file(filename, wcs=None):
    catname = filename + '.txt'

    if not posixpath.exists(catname):
        return None

    if not wcs:
        header = pyfits.getheader(filename, -1)
        wcs = pywcs.WCS(header)

    with open(catname, 'r') as file:
        lines = file.readlines()
        x = np.empty(len(lines))
        y = np.empty(len(lines))
        flux = np.empty(len(lines))
        fluxerr = np.empty(len(lines))

        for i in xrange(len(lines)):
            s = lines[i].split()
            x[i] = float(s[0])
            y[i] = float(s[1])
            flux[i] = float(s[2])
            fluxerr[i] = float(s[6])

        obj = {}
        obj['x'], obj['y'] = x, y
        obj['ra'], obj['dec'] = wcs.all_pix2world(x, y, 0)
        obj['flux'] = flux
        obj['fluxerr'] = fluxerr
        obj['mag'] = -2.5*np.log10(obj['flux'])
        obj['magerr'] = 2.5*np.log10(1 + fluxerr/flux)

    return obj

def get_zero_point(obj, cat, sr0=50.0, maglim=None):
    zero = {}

    h = htm.HTM(10)
    m = h.match(obj['ra'], obj['dec'], cat['ra'], cat['dec'], sr0*1.0/3600, maxmatch=0)
    a = [0,0,0]
    sd = 0.0

    oidx = m[0]
    cidx = m[1]
    dist = m[2]*3600

    maxdist = 3.0*np.std(dist)
    print "Max distance = %g arcsec" % maxdist
    zero['maxdist'] = maxdist

    mag_b = cat['b'][cidx]
    mag_v = cat['v'][cidx]
    mag_r = cat['r'][cidx]

    mag_bv = mag_b - mag_v
    mag_vr = mag_v - mag_r

    mag_instr = obj['mag'][oidx]
    x = obj['x'][oidx]
    y = obj['y'][oidx]

    zero['mag_b'] = mag_b
    zero['mag_v'] = mag_v
    zero['mag_r'] = mag_r
    zero['mag_instr'] = mag_instr

    weights = 10**(-0.4*mag_v)

    idx = (dist < 3.0*np.std(dist))
    if maglim:
        idx = np.logical_and(idx, mag_v < 8)

    for iter in range(3):
        print len(dist[idx])

        sigma = np.std(mag_instr[idx] - mag_v[idx])
        mag0 = np.mean(mag_instr[idx] - mag_v[idx])

        #idx = np.logical_and(idx, np.abs(mag_instr - mag_v - mag0) < 3.0*sigma)

        #plt.clf(); plt.scatter(mag_v[idx], mag_instr[idx])

        #X = np.vstack([np.ones(len(mag_instr)), x*x, x, x*y, y, y*y, mag_bv, mag_bv**2, mag_vr, mag_vr**2]).T
        #X = np.vstack([np.ones(len(mag_instr)), x*x, x, x*y, y, y*y]).T
        X = np.vstack([np.ones(len(mag_instr))]).T
        Y = mag_v - mag_instr
        C2 = sm.WLS(Y[idx], X[idx], weights=weights[idx]).fit().params
        print sm.WLS(Y[idx], X[idx], weights=weights[idx]).fit().summary()
        #mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y + C2[6]*mag_bv + C2[7]*mag_bv**2 + C2[8]*mag_vr + C2[9]*mag_vr**2
        #mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y
        mag0 = np.repeat(C2[0], len(x))
        sigma2 = np.std(mag0[idx] + mag_instr[idx]  - mag_v[idx])
        print C2, sigma2

        idx = np.logical_and(idx, np.abs(mag0 + mag_instr - mag_v) < 3.0*sigma2)

        # plt.clf()
        # plt.scatter(mag_instr, mag0 + mag_instr - mag_v, s=1)
        # plt.scatter(mag_instr[idx], mag0[idx] + mag_instr[idx] - mag_v[idx], marker='.', c='red', s=3)

        pass

    zero['mag0'] = C2

    return zero

def get_zero_point_simple(obj, cat, sr0=50.0, filter='Clear', maglim=None, spatial=False):
    zero = {}

    h = htm.HTM(10)
    m = h.match(obj['ra'], obj['dec'], cat['ra'], cat['dec'], sr0*1.0/3600, maxmatch=0)
    a = [0,0,0]
    sd = 0.0

    oidx = m[0]
    cidx = m[1]
    dist = m[2]*3600

    maxdist = 3.0*np.std(dist)
    print "Max distance = %g arcsec" % maxdist
    zero['maxdist'] = maxdist

    if filter == 'B':
        mag_cat = cat['b'][cidx]
    elif filter == 'V':
        mag_cat = cat['v'][cidx]
    elif filter == 'R':
        mag_cat = cat['r'][cidx]
    elif filter == 'U':
        mag_cat = cat['u'][cidx]
    else:
        mag_cat = cat['v'][cidx]

    mag_instr = obj['mag'][oidx]

    x = obj['x'][oidx]
    y = obj['y'][oidx]

    zero['mag_cat'] = mag_cat
    zero['mag_instr'] = mag_instr
    zero['dist'] = dist

    zero['x'] = x
    zero['y'] = y

    #weights = 10**(-0.4*mag_cat)
    weights = np.repeat(1.0, len(mag_cat))

    idx = (dist < 3.0*np.std(dist))
    if maglim:
        idx = np.logical_and(idx, mag_cat < maglim)

    mag0 = 0

    for iter in range(3):
        print len(dist[idx])

        if spatial and len(dist[idx]) < 5:
            break
        elif len(dist[idx]) < 2:
            break

        sigma = np.std(mag_instr[idx] - mag_cat[idx])
        mag0 = np.mean(mag_instr[idx] - mag_cat[idx])

        #idx = np.logical_and(idx, np.abs(mag_instr - mag_v - mag0) < 3.0*sigma)

        if spatial:
            X = np.vstack([np.ones(len(mag_instr)), x*x, x, x*y, y, y*y]).T
        else:
            X = np.vstack([np.ones(len(mag_instr))]).T

        Y = mag_cat - mag_instr

        C2 = sm.WLS(Y[idx], X[idx], weights=weights[idx]).fit().params

        if spatial:
            mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y
        else:
            mag0 = np.repeat(C2[0], len(x))

        sigma2 = np.std(mag0[idx] + mag_instr[idx]  - mag_cat[idx])
        print C2, sigma2

        idx = np.logical_and(idx, np.abs(mag0 + mag_instr - mag_cat) < 3.0*sigma2)

    zero['C2'] = C2
    zero['mag0s'] = mag0

    zero['sigma'] = sigma2
    zero['sigma0'] = np.std(mag0 + mag_instr  - mag_cat)
    zero['idx'] = idx

    if spatial:
        x = obj['x']
        y = obj['y']
        mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y
    else:
        mag0 = C2[0]

    zero['mag0'] = mag0

    zero['mag'] = obj['mag'] + mag0

    return zero

def clean_image(image):
    h,w = image.shape

    for x in xrange(w):
        image[:h/2, x] -= np.median(image[:h/2, x]) # (3.0*np.median(image[:h/2, x]) - 2.0*np.mean(image[:h/2, x]))
        image[h/2:, x] -= np.median(image[h/2:, x]) # (3.0*np.median(image[h/2:, x]) - 2.0*np.mean(image[h/2:, x]))

    return image

def calibrate_image(filename, dark='', flat='', simple=True, match=True, clean=False, maglim=None):
    print "Performing photometric calibration of file %s" % filename

    res = {}

    image = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    f = Favor2()

    if match or not has_wcs(filename):
        print "No WCS info, blindmatching"
        wcs = f.blind_match_file(filename)
    else:
        wcs = pywcs.WCS(header=header)

    res['wcs'] = wcs

    if not wcs:
        print "WCS match failure"
        return res

    ra0, dec0, sr0 = get_frame_center(header=header, wcs=wcs)
    res['ra0'], res['dec0'], res['sr0'] = ra0, dec0, sr0

    # Saturated pixels, to be masked after the flatfielding
    #isat = image == np.max(image)
    isat = image >= 30000

    if dark:
        idark = pyfits.getdata(dark, -1)

        image = 1.0*image - idark

        if flat:
            iflat = pyfits.getdata(flat, -1)
            iflat = 1.0*iflat - idark

            image *= np.mean(iflat)/iflat

    if clean:
        image = clean_image(image)

    image[isat] = 65535

    # Detect objects
    obj = get_objects(image=image, wcs=wcs)

    print "%d objects detected" % (len(obj['flux']))

    res['filter'] = header['FILTER']
    res['mag_calibrated'] = False

    if len(obj['flux']) > 1:
        # Get the catalogue
        cat = f.get_stars(ra0, dec0, sr0, catalog='pickles')

        zero = get_zero_point_simple(obj, cat, filter=res['filter'], maglim=maglim)

        if zero:
            mag0 = zero['mag0']

            if mag0:
                res['mag_calibrated'] = True
    else:
        mag0 = 0

    res['mag0'] = mag0

    return res

if __name__ == '__main__':
    pass
