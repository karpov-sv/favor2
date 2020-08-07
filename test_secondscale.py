#!/usr/bin/env python

import pyfits, pywcs
import numpy as np
import os, shutil
import tempfile
import posixpath

import statsmodels.api as sm

import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
plt.ion()

def scatter3d(x, y, z, **kwargs):
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')
    ax.scatter(x, y, z, **kwargs)

from sextractor import Sextractor
from favor2 import Favor2

from esutil import coords, htm

def breakpoint():
    try:
        from IPython.core.debugger import Tracer
        Tracer()()
    except:
        import pdb
        pdb.set_trace()

f = Favor2()

def has_wcs(filename=None, header=None):
    if not header:
        header = pyfits.getheader(filename, -1)

    w = pywcs.WCS(header=header)

    return w.wcs.has_cd()

def get_frame_center(filename=None, header=None):
    if not header:
        header = pyfits.getheader(filename, -1)

    w = pywcs.WCS(header=header)

    [ra1],[dec1] = w.all_pix2sky(0, 0, 1)
    [ra0],[dec0] = w.all_pix2sky(header['NAXIS1']/2, header['NAXIS2']/2, 1)

    sr = coords.sphdist(ra0, dec0, ra1, dec1)[0]

    return ra0, dec0, sr

def get_objects(image=None, header=None, filename=None):
    if image == None:
        image = pyfits.getdata(filename, -1)
        header = pyfits.getheader(filename, -1)

    sex = Sextractor()

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
                      "XWIN_IMAGE", "YWIN_IMAGE",
                      "A_IMAGE", "B_IMAGE", "THETA_IMAGE", "FLAGS", "ELLIPTICITY",
                      "FWHM_IMAGE", "BACKGROUND",
                      "FLUX_AUTO", "FLUXERR_AUTO", "FLUX_APER", "FLUXERR_APER",
                      "ERRX2WIN_IMAGE", "ERRY2WIN_IMAGE",
                      "A_WORLD", "B_WORLD", "THETA_WORLD",
                      "XWIN_WORLD", "YWIN_WORLD", "ERRX2WIN_WORLD", "ERRY2WIN_WORLD"],
            options = {'BACKPHOTO_TYPE':'GLOBAL',
                       'BACK_SIZE': 128,
                       'BACK_FILTERSIZE': 3,
                       'DETECT_MINAREA': 2,
                       'DETECT_THRESH':2,
                       'ANALYSIS_THRESH':2,
                       'DEBLEND_MINCONT': 0.001,
                       'SATUR_LEVEL': 65534,
                       'PHOT_APERTURES': 4.0*fwhm,
                       'CHECKIMAGE_TYPE': 'BACKGROUND,BACKGROUND_RMS,FILTERED,OBJECTS,-OBJECTS',
                       'CHECKIMAGE_NAME': 'out_bg.fits,out_bg_rms.fits,out_filtered.fits,out_objects.fits,out_mobjects.fits',
                   },
            parse=False)

    s0 = sex.table

    # Filter extracted objects
    idx = (s0['FLAGS'] == 0)
    idx = np.logical_or(idx, s0['FLAGS'] == 2)

    idx = np.logical_and(idx, np.isfinite(s0['FLUX_APER']))
    idx = np.logical_and(idx, np.isfinite(s0['FLUXERR_APER']))

    idx = np.logical_and(idx, s0['FLUX_APER'] > 0)
    #idx = np.logical_and(idx, s0['FLUX_APER']/s0['FLUXERR_APER'] > 10)

    #idx = np.logical_and(idx, s0['ELLIPTICITY'] < 0.2)

    if False or image.shape == (1032,1392):
        # MMT-6, let's reject events on frame edges etc
        idx = np.logical_and(idx, s0['XWIN_IMAGE'] < image.shape[1]-100)
        idx = np.logical_and(idx, s0['XWIN_IMAGE'] > 100)
        idx = np.logical_and(idx, s0['YWIN_IMAGE'] < image.shape[0]-100)
        idx = np.logical_and(idx, s0['YWIN_IMAGE'] > 100)

    # We will return the dict of Numpy arrays
    s = {}
    for name in s0.names:
        s[name] = s0[idx][name]

    # Rename some columns for convenience
    s['mag'] = s['MAG_APER']
    #s['mag'] = s['MAG_AUTO']
    s['mag_err'] = s['MAGERR_APER']
    s['flux'] = s['FLUX_APER']
    s['flux_err'] = s['FLUXERR_APER']
    s['x'] = s['XWIN_IMAGE']
    s['y'] = s['YWIN_IMAGE']

    wcs = pywcs.WCS(header=header)

    s['ra'], s['dec'] = wcs.all_pix2sky(s['XWIN_IMAGE'], s['YWIN_IMAGE'], 1)

    return s

def get_objects_fistar(image=None, header=None, filename=None):
    if image == None:
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

    s['ra'], s['dec'] = wcs.all_pix2sky(s['x'], s['y'], 1)
    s['mag'] = s['magnitude']

    return s

def get_objects_file(filename, header, params=['x', 'y', 'flux', 'v']):
    s = {}

    with open(filename, 'r') as file:
        lines = file.readlines()
        N = len(lines)

        for iparam,param in enumerate(params):
            arr = np.empty(N)

            for i,line in enumerate(lines):
                sl = line.split()
                arr[i] = float(sl[iparam])

            s[param] = arr

    wcs = pywcs.WCS(header=header)

    s['ra'], s['dec'] = wcs.all_pix2sky(s['x'], s['y'], 1)
    s['mag'] = -2.5*np.log10(s['flux'])

    return s

def get_zero_point(obj, cat, sr0=20.0, maglim=8.0):
    h = htm.HTM(10)
    m = h.match(obj['ra'], obj['dec'], cat['ra'], cat['dec'], sr0*1.0/3600, maxmatch=0)
    a = [0,0,0]
    sd = 0.0

    oidx = m[0]
    cidx = m[1]
    dist = m[2]*3600

    maxdist = 3.0*np.std(dist)
    print "Max distance = %g arcsec" % maxdist

    mag_b = cat['b'][cidx]
    mag_v = cat['v'][cidx]
    mag_r = cat['r'][cidx]

    mag_bv = mag_b - mag_v
    mag_vr = mag_v - mag_r

    mag_instr = obj['mag'][oidx]
    x = obj['x'][oidx]
    y = obj['y'][oidx]

    #weights = 10**(-0.4*mag_v)
    weights = np.ones(len(oidx))

    idx = (dist < 3.0*np.std(dist))
    #idx = np.logical_and(idx, mag_v < 8)

    for iter in range(3):
        print len(dist[idx])

        sigma = np.std(mag_instr[idx] - mag_v[idx])
        mag0 = np.mean(mag_instr[idx] - mag_v[idx])

        #idx = np.logical_and(idx, np.abs(mag_instr - mag_v - mag0) < 3.0*sigma)

        plt.clf(); plt.scatter(mag_v[idx], mag_instr[idx])

        #X = np.vstack([np.ones(len(mag_instr)), x*x, x, x*y, y, y*y, mag_bv, mag_bv**2, mag_vr, mag_vr**2]).T
        X = np.vstack([np.ones(len(mag_instr)), x*x, x, x*y, y, y*y, mag_bv, mag_vr]).T
        #X = np.vstack([np.ones(len(mag_instr)), x*x, x, x*y, y, y*y]).T
        Y = mag_v - mag_instr
        C2 = sm.WLS(Y[idx], X[idx], weights=weights[idx]).fit().params
        print sm.WLS(Y[idx], X[idx], weights=weights[idx]).fit().summary()
        #mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y + C2[6]*mag_bv + C2[7]*mag_bv**2 + C2[8]*mag_vr + C2[9]*mag_vr**2
        mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y + C2[6]*mag_bv + C2[7]*mag_vr
        #mag0 = C2[0] + C2[1]*x*x + C2[2]*x + C2[3]*x*y + C2[4]*y + C2[5]*y*y
        sigma2 = np.std(mag0[idx] + mag_instr[idx]  - mag_v[idx])
        print C2, sigma2

        idx = np.logical_and(idx, np.abs(mag0 + mag_instr - mag_v) < 3.0*sigma2)

        plt.clf()
        # plt.scatter(mag_instr, mag0 + mag_instr - mag_v, s=1)
        # plt.scatter(mag_instr[idx], mag0[idx] + mag_instr[idx] - mag_v[idx], marker='.', c='red', s=3)
        plt.scatter(mag_v, mag0 + mag_instr - mag_v, s=1)
        #plt.scatter(mag_instr[idx], mag0[idx] + mag_instr[idx] - mag_v[idx], marker='.', c='red', s=3)

        pass

    plt.show()

    breakpoint()

    return C2

def clean_image(image):
    h,w = image.shape

    print "Cleaning image"

    for y in xrange(h):
        v_mean = np.mean(image[y, :])
        v_median = np.median(image[y, :])

        if v_mean < v_median:
            image[y, :] -= 3.0*v_median - 2.0*v_mean
        else:
            image[y, :] -= v_median

    for x in xrange(w):
        image[:h/2, x] -= np.median(image[:h/2, x]) # (3.0*np.median(image[:h/2, x]) - 2.0*np.mean(image[:h/2, x]))
        image[h/2:, x] -= np.median(image[h/2:, x]) # (3.0*np.median(image[h/2:, x]) - 2.0*np.mean(image[h/2:, x]))

    return image

def process_image(filename, dark='', flat=''):
    print filename

    if not has_wcs(filename):
        print "No WCS info, skipping"
        return

    image = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

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

    #image = clean_image(image)
    #pyfits.writeto('out.fits', image, clobber=True)
    #return

    image[isat] = 65535


    # Detect objects
    obj = get_objects(image=image, header=header)
    #obj = get_objects_fistar(image=image, header=header)
    #obj = get_objects_file('/Users/karpov/Downloads/20141013_170758_8.avg.aper.dat', header)

    # obj1 = obj
    # obj2 = get_objects_file('/Users/karpov/Downloads/20141013_170758_8.avg.aper.dat', header)
    # h = htm.HTM(10)
    # m = h.match(obj1['ra'], obj1['dec'], obj2['ra'], obj2['dec'], 20.0*1.0/3600, maxmatch=0)
    # idx1 = m[0]
    # idx2 = m[1]
    # plt.clf()
    # plt.scatter(obj1['mag'][idx1], obj2['mag'][idx2], s=1)
    # plt.plot([-12, -2], [-12, -2])

    # breakpoint()

    print "%d objects detected" % (len(obj['flux']))

    # detected objects
    # for i in xrange(len(obj['mag'])):
    #     ds9.set_region("J2000;cross point(%g,%g) # color=blue" % (obj['ra'][i], obj['dec'][i]))

    ra0, dec0, sr0 = get_frame_center(header=header)

    # Get the catalogue
    #cat = f.get_stars(ra0, dec0, sr0, catalog='tycho2')
    cat = f.get_stars(ra0, dec0, sr0, catalog='pickles')
    #cat = f.get_stars(ra0, dec0, sr0, catalog='wbvr')
    #cat = f.get_stars(ra0, dec0, sr0, catalog='nomad')

    mag0 = get_zero_point(obj, cat)

    # plt.clf()
    # plt.hist(obj['mag'] + mag0, bins=100)

    # for i in xrange(len(obj['mag'])):
    #     o = {k:v[i] for k,v in obj.iteritems()}
    #     if o['flux'] > 0:
    #         o['mag'] -= mag0
    #         o['time'] = img[0].header.get('TIME')
    #         o['filter'] = img[0].header.get('FILTER')
    #         f.store_record(o)

    return mag0

def process_night(night):
    print "Processing %s" % (night)

    images = f.query("select * from images where night = %s and type = 'avg'", (night,), simplify=False)

    for image in images:
        filename = image['filename']
        dark = f.find_image(image['time'], 'dark')
        flat = f.find_image(image['time'], 'flat')

        if filename and dark and flat:
            process_image(filename, dark, flat)

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    # parser.add_option('-b', '--base', help='base path, default to ./', action='store', dest='base', default='')
    # parser.add_option('-d', '--dbname', help='database name, defaults to favor2', action='store', dest='dbname', default='favor2')
    # parser.add_option('-a', '--all', help='Process all objects, even already processed', action='store_true', dest='process_all', default=False)
    # parser.add_option('-n', '--night', help='Night to process, defaults to all', action='store', dest='night', default='')

    (options, args) = parser.parse_args()

    print args

    if args:
        for arg in args:
            process_image(arg)
    else:
        #process_image('tmp/60s/20140803_234614_1.survey.new')
        #process_image('tmp/MMT1/20140701_202312_1.avg.fits', dark='tmp/MMT1/20140723_182643_1.dark.fits')
        process_image('tmp/photo/20141013_170758_8.avg.fits', dark='tmp/photo/20141013_152036_8.dark.fits')


    # if options.night:
    #     process_night(options.night)

# night = '2014_01_28'

# images = f.query("select * from images where night = %s and type = 'avg'", (night,), simplify=False)

# for image in [images[0]]:
#     process_image(image['filename'])

#process_image('tmp/secondscale/20140201_211910.avg.fits', dark='tmp/secondscale/20140201_203949.dark.fits', flat='tmp/secondscale/20140201_204747.flat.fits')
#process_image("/Users/karpov/Downloads/new-image-7.fits")
#process_image('tmp/MMT1/20140701_202312_1.avg.fits', dark='tmp/MMT1/20140723_182643_1.dark.fits')
#process_image('tmp/60s/20140803_234614_1.survey.new')
