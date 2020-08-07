#!/usr/bin/env python
import os
import posixpath
import re
import datetime
import tempfile
import glob

from astropy import wcs as pywcs
from astropy.io import fits as pyfits

import numpy as np
import scipy.ndimage as ndimage
import scipy.optimize as optimize
import cv2
import sklearn.cluster as cluster
from skimage import morphology

import coord, ephem

import secondscale as ss

from favor2 import Favor2, fix_remote_path

def has_wcs(filename=None, header=None):
    if not header:
        header = pyfits.getheader(filename, -1)

    w = pywcs.WCS(header=header)

    if w.wcs.has_cd() and w.wcs.ctype[0] == 'RA---TAN-SIP':
        return True

    return False

def sum_by_group(values, groups):
    order = np.argsort(groups)
    groups = groups[order]
    values = values[order]
    values.cumsum(out=values)
    index = np.ones(len(groups), 'bool')
    index[:-1] = groups[1:] != groups[:-1]
    values = values[index]
    groups = groups[index]
    values[1:] = values[1:] - values[:-1]
    return values, groups

# Define model function to be used to fit to the data above:
def gauss_fn(x, *p):
    A, B, mu, sigma = p
    return A*np.exp(-(x-mu)**2/(2.*sigma**2)) + B

def get_fwhm(x, y):
    p0 = [np.max(y)-np.min(y), np.min(y), 0.0, 3.0]
    coeff,_ = optimize.curve_fit(gauss_fn, x, y, p0)

    return 2.35482*coeff[3]

def get_gauss_fit(x, y):
    p0 = [np.max(y)-np.min(y), np.min(y), 0.0, 3.0]
    coeff,_ = optimize.curve_fit(gauss_fn, x, y, p0)

    return coeff

# Filter out outliers far from main cluster in boolean array
def filter_isolated_idxs(idxs=[], maxdist=3.0):
    newidxs = idxs.copy()

    seq = np.where(idxs==True)[0]

    if len(seq):
        X = np.array(zip(seq, np.zeros(len(seq))))
        cfn = cluster.DBSCAN(eps=3, min_samples=1)
        cfn.fit(X)

        clusters = {}
        for v in np.unique(cfn.labels_):
            clusters[v] = len(cfn.labels_[cfn.labels_==v])

        maxv = sorted(clusters, key=clusters.get)[-1]

        for i in xrange(len(cfn.labels_)):
            if cfn.labels_[i] != maxv:
                newidxs[seq[i]] = False

    return newidxs

def filter_isolated_idxs_new(idxs=[], maxdist=3.0):
    clusters = []
    cluster = []

    newidxs = np.zeros_like(idxs)

    for i in xrange(len(idxs)):
        if idxs[i]:
            cluster.append(i)

        if (i == len(idxs) or not idxs[i]) and len(cluster):
            clusters.append(cluster)
            cluster=[]

    for i in xrange(len(clusters)):
        valid = True
        if len(clusters[i]) <= 1:
            valid = False

        if valid:
            newidxs[clusters[i]] = True
            #print clusters[i], m.lcs[0][clusters[i]]

    return newidxs

def replaceFileHeader(filename, new_header, remove=['DATE-OBS', 'TIME-OBS', 'JD', 'TIME', 'HISTORY', 'COMMENT']):
    data = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    copy = new_header.copy()

    for kw in remove:
        while copy.get(kw):
            copy.remove(kw)

    header.update(copy)

    # TODO: add FITS compression!
    pyfits.writeto(filename, data, header, clobber=True)

class Meteor:
    def __init__(self, path = '', id = 0, half_width = 10, load = False, wcs_order=2, width=2560, height=2160, rough=False, excess=4, favor2=None):
        self.favor2 = favor2 or Favor2()
        self.original_width = width
        self.original_height = height
        self.half_width = half_width
        self.header = None
        self.frames = []
        self.channels = []

        self.wcs_order = wcs_order
        self.rough = rough
        self.excess = excess

        if id:
            res = self.favor2.query('SELECT * FROM realtime_objects WHERE state=0 AND id=%s', (id,))
            if res:
                path = posixpath.join('ARCHIVE', 'meteor', res['night'], str(id))
                print path
        if path:
            if load:
                self.loadMeteor(path)
            else:
                self.analyzeMeteor(path)
        else:
            self.path = None

    def loadFrames(self):
        files = os.listdir(self.path)
        files.sort()
        files = [file for file in files if re.match('^.*crop.fits$', file)]

        print "%d images" % len(files)

        if not len(files):
            return

        print "Accumulating images"

        self.max_image = None
        self.min_image = None
        self.header = None
        self.frames = []
        self.shape = None

        for filename in files:
            image = pyfits.getdata(posixpath.join(self.path, filename))
            header = pyfits.getheader(posixpath.join(self.path, filename), -1)
            time = datetime.datetime.strptime(header['TIME'], '%d.%m.%Y %H:%M:%S.%f')

            if not self.header:
                self.header = header
                self.x0 = header['CROP_X0']
                self.y0 = header['CROP_Y0']

            if not self.shape:
                self.shape = image.shape
                self.height, self.width = image.shape

            if self.max_image is not None:
                self.max_image = np.maximum(image, self.max_image)
            else:
                self.max_image = image

            if self.min_image is not None:
                self.min_image = np.minimum(image, self.min_image)
            else:
                self.min_image = image

            self.frames.append({'image':image, 'time':time, 'header':header, 'filename':filename})

        pyfits.writeto(posixpath.join(self.out_path, 'max.fits'), self.max_image, header=self.header, clobber=True)

        print "Computing median"

        if os.path.isfile(posixpath.join(self.out_path, 'median.fits')):
            self.median_image = pyfits.getdata(posixpath.join(self.out_path, 'median.fits'))
        else:
            self.median_image = np.median([f['image'] for f in self.frames], axis=0)
            pyfits.writeto(posixpath.join(self.out_path, 'median.fits'), self.median_image, header=self.header, clobber=True)

        if os.path.isfile(posixpath.join(self.out_path, 'sigma.fits')):
            self.sigma_image = pyfits.getdata(posixpath.join(self.out_path, 'sigma.fits'))
        else:
            self.sigma_image = np.std([f['image'] for f in self.frames], axis=0)
            pyfits.writeto(posixpath.join(self.out_path, 'sigma.fits'), self.sigma_image, header=self.header, clobber=True)

        if os.path.isfile(posixpath.join(self.out_path, 'mean.fits')):
            self.mean_image = pyfits.getdata(posixpath.join(self.out_path, 'mean.fits'))
        else:
            self.mean_image = np.mean([f['image'] for f in self.frames], axis=0)
            pyfits.writeto(posixpath.join(self.out_path, 'mean.fits'), self.mean_image, header=self.header, clobber=True)

        if os.path.isfile(posixpath.join(self.out_path, 'diff_im.fits')):
            self.diff_im = pyfits.getdata(posixpath.join(self.out_path, 'diff_im.fits'))
        else:
            self.diff_im = self.max_image - self.median_image
            idx = np.where(self.sigma_image > 0)
            self.diff_im[idx] = self.diff_im[idx]/self.sigma_image[idx]

    def refineWCS(self):
        fd, tmpname = tempfile.mkstemp(suffix='.fits')
        os.close(fd)

        self.wcs = self.favor2.blind_match_file(posixpath.join(self.out_path, 'median.fits'), outfile=tmpname, order=self.wcs_order)

        if posixpath.isfile(tmpname):
            print "Successfully refined WCS using median frame"
            self.header = pyfits.getheader(tmpname, -1)
            os.unlink(tmpname)
            return True
        else:
            print "WCS refinement failed, using original WCS"
            return False

    def analyzeMeteor(self, path=''):
        self.path = path
        self.out_path = posixpath.join(path, 'analysis')

        if not os.path.isdir(path):
            print "Not a directory"
            return

        try:
            os.makedirs(self.out_path)
        except:
            pass

        self.analyzed = False
        self.laser = False

        print "Processing %s" % self.path

        self.loadFrames()

        print "Hough transform"

        self.diff_im -= np.median(self.diff_im)

        self.mask = ndimage.gaussian_filter(self.diff_im, 1)
        thresh = np.percentile(self.mask, 95)
        _,self.mask = cv2.threshold(self.mask.astype(np.float32), thresh, 1, cv2.THRESH_BINARY)

        pyfits.writeto(posixpath.join(self.out_path, 'mask.fits'), self.mask.astype(np.uint8), header=self.header, clobber=True)

        # Results are rho, theta: rho = x*cos(theta) + y*sin(theta)
        lines = cv2.HoughLines(self.mask.astype(np.uint8), 1, np.pi*0.5/180, 100)

        # Try to refine
        if lines is not None and lines.shape[1]:
            mask2 = morphology.skeletonize(self.mask)
            lines2 = cv2.HoughLines(mask2.astype(np.uint8), 1, np.pi*0.5/180, 100)

            if lines2 is not None:
                for line in lines[0]:
                    if line[0] or line[1]:
                        rho, theta = line
                        lines2 = np.array([[[r1, t1] for r1,t1 in lines[0] if np.abs(r1-rho) < 10 and np.abs(t1-theta)<0.1]])

                        if lines2.shape[1]:
                            lines = lines2

                        break

        # print "Lines:"
        # print lines

        self.a_start = []
        self.a_end = []
        self.x_start = []
        self.x_end = []
        self.y_start = []
        self.y_end = []
        self.cropped_start = []
        self.cropped_end = []
        self.time = []
        self.flux = []
        self.diff_ims = []
        self.lcs = []

        if lines is None or not lines.shape[1]:
            print "Can't detect meteor trail!"
            return

        for line in lines[0]:
            if line[0] or line[1]:
                rho, theta = line
                break
            else:
                rho, theta = (0, 0)

        if not rho and not theta:
            print "No sensible trail found"
            return

        print "rho=%g theta=%g" % (rho, theta)

        # Now we have median image and may try to refine the original WCS
        if self.refineWCS():
            for filename in glob.glob(posixpath.join(self.out_path, '*.fits')):
                replaceFileHeader(filename, self.header)
            for filename in glob.glob(posixpath.join(self.path, '*.fits')):
                replaceFileHeader(filename, self.header)

        y,x = np.mgrid[0:self.shape[0],0:self.shape[1]]
        a = y*np.cos(theta) - x*np.sin(theta)
        b = rho - x*np.cos(theta) - y*np.sin(theta)

        self.rho = rho
        self.theta = theta

        # Estimate FWHM
        self.fwhm_bins = np.arange(-2.0*self.half_width, 2.0*self.half_width, 1.0)
        self.fwhm_sums,idxs = sum_by_group(self.diff_im.flatten(), np.digitize(b.flatten(), self.fwhm_bins))
        # self.fwhm = get_fwhm(self.fwhm_bins[:-1], self.fwhm_sums[1:-1])

        if len(idxs) < 0.7*len(self.fwhm_bins):
            return

        self.fwhm = get_fwhm(self.fwhm_bins[idxs[1:-1]], self.fwhm_sums[1:-1])
        print "FWHM=%.2g" % self.fwhm
        self.fwhm = max(2.0, min(0.5*self.half_width, self.fwhm))

        idx = np.abs(b) < 2.0*self.fwhm

        # a rounded to half_width
        aa = ((a - np.min(a[idx]))/self.half_width).astype(np.int64)

        self.lc_zero = None
        self.sd = None

        self.lcs0 = []

        for frame in self.frames:
            image = frame['image']
            time = frame['time']

            if self.rough:
                diff_im = image - self.min_image
                diff_im -= np.median(diff_im)
            else:
                diff_im = image - self.median_image

            idx0 = np.where(self.sigma_image > 0)
            mean_sigma = np.mean(self.sigma_image[idx0])
            diff_im_norm = diff_im.copy()
            diff_im_norm[idx0] = diff_im[idx0]/self.sigma_image[idx0]*mean_sigma

            #pyfits.writeto(posixpath.join(self.out_path, 'diff_%s.fits' % time), diff_im, header=self.header, clobber=True)

            # Rebin along a axis. Throw away first and last bins
            nw = np.bincount(aa[idx], weights=diff_im_norm[idx])[1:-1]
            nc = np.bincount(aa[idx])[1:-1]
            na = np.bincount(aa[idx], weights=a[idx])[1:-1]
            nx = np.bincount(aa[idx], weights=x[idx])[1:-1]
            ny = np.bincount(aa[idx], weights=y[idx])[1:-1]

            lc = nw/nc # Light curve normalized to 1 pixel. Noise is generally not normal!
            a0 = na/nc
            x0 = nx/nc
            y0 = ny/nc

            self.lcs0.append(lc)

            if self.rough:
                if self.lc_zero is None:
                    self.lc_zero = 2.5*np.median(lc)-1.5*np.mean(lc) # Moda

                if self.sd is None:
                    self.sd = 1.4826*np.median(np.abs(lc - np.median(lc))) # MAD

                lc -= self.lc_zero
            else:
                if not self.sd:
                    self.sd = np.std(lc[lc != 0]) # stddev for regions inside the frame only

            w = np.where(filter_isolated_idxs(lc > self.excess*self.sd))

            if w[0].shape[0] > 1 and np.max(w[0])-np.min(w[0]) > 1:
                a_start = a0[np.min(w[0])]
                x_start = x0[np.min(w[0])]
                y_start = y0[np.min(w[0])]
                a_end = a0[np.max(w[0])]
                x_end = x0[np.max(w[0])]
                y_end = y0[np.max(w[0])]

                self.a_start.append(a_start)
                self.x_start.append(x_start)
                self.y_start.append(y_start)

                self.a_end.append(a_end)
                self.x_end.append(x_end)
                self.y_end.append(y_end)
                self.time.append(time)

                self.cropped_start.append(self.x0 + x_start <= 2.0*self.half_width or
                                          self.y0 + y_start <= 2.0*self.half_width or
                                          self.x0 + x_start >= self.original_width - 2.0*self.half_width or
                                          self.y0 + y_start >= self.original_height - 2.0*self.half_width)

                self.cropped_end.append(self.x0 + x_end <= 2.0*self.half_width or
                                          self.y0 + y_end <= 2.0*self.half_width or
                                          self.x0 + x_end >= self.original_width - 2.0*self.half_width or
                                          self.y0 + y_end >= self.original_height - 2.0*self.half_width)

                if not self.lcs:
                    self.lcs.append(a0)

                self.lcs.append(lc)

                self.diff_ims.append(diff_im)

                idx1 = np.abs(b) < 2.0*self.fwhm
                idx1 = np.logical_and(idx1, a >= min(a_start, a_end))
                idx1 = np.logical_and(idx1, a <= max(a_start, a_end))
                flux = np.sum(diff_im[idx1])

                self.flux.append(flux)

        if self.a_start:
            if len(self.a_start) > 1:
                sc1,_ = np.polyfit([(t - self.time[0]).total_seconds() for t in self.time], self.a_start, 1)
                sc2,_ = np.polyfit([(t - self.time[0]).total_seconds() for t in self.time], self.a_end, 1)

                if sc1 < 0 and sc2 < 0:
                    print "Backward motion, reverting"
                    self.a_start,self.a_end = self.a_end, self.a_start
                    self.x_start,self.x_end = self.x_end, self.x_start
                    self.y_start,self.y_end = self.y_end, self.y_start
                    self.cropped_start,self.cropped_end = self.cropped_end, self.cropped_start

            # WCS
            if has_wcs(header=self.header):
                self.wcs = pywcs.WCS(header=self.header)
            else:
                self.wcs = self.favor2.blind_match_file(posixpath.join(path, self.frames[0]['filename']))

            if not self.wcs:
                print "No WCS, exiting"
                return

            self.pixscale = np.sqrt(self.wcs.wcs.cd[0][0]**2 + self.wcs.wcs.cd[1][0]**2)

            self.ra_start,self.dec_start = self.wcs.all_pix2world(self.x_start, self.y_start, 1)
            self.ra_end,self.dec_end = self.wcs.all_pix2world(self.x_end, self.y_end, 1)

            self.nframes = len(self.a_start)

            # Zenith distance
            self.favor2.obs.date = self.frames[0]['time']
            p = ephem.FixedBody()
            p._ra = self.ra_start[0]*ephem.pi/180.0
            p._dec = self.dec_start[0]*ephem.pi/180.0
            p.compute(self.favor2.obs)
            self.z_start = 90.0 - 180.0*p.alt/ephem.pi

            p._ra = self.ra_end[-1]*ephem.pi/180.0
            p._dec = self.dec_end[-1]*ephem.pi/180.0
            p.compute(self.favor2.obs)
            self.z_end = 90.0 - 180.0*p.alt/ephem.pi

            # Photometry
            self.mag_calibrated = 0
            self.mag0 = 0
            self.filter = 'Clear'
            full_image = self.favor2.find_image(self.time[0], 'avg', channel_id=self.header['CHANNEL ID'])
            full_dark = self.favor2.find_image(self.time[0], 'dark', channel_id=self.header['CHANNEL ID'])
            if full_image:
                full_image = fix_remote_path(full_image, channel_id=self.header['CHANNEL ID'])
            else:
                full_image = posixpath.join(self.out_path, 'median.fits')
            if full_dark:
                full_dark = fix_remote_path(full_dark, channel_id=self.header['CHANNEL ID'])

            if full_image and posixpath.exists(full_image):
                res = ss.calibrate_image(full_image, dark=full_dark, match=True, maglim=11.0)
                if res:
                    self.mag0 = res['mag0']
                    self.filter = res['filter']
                    if res['mag_calibrated']:
                        self.mag_calibrated = 1
            else:
                print "No images for photometric calibration: %s" % full_image

            self.mag = [self.mag0 - 2.5*np.log10(f) for f in self.flux]
            self.mag_min = min(self.mag)
            self.mag_max = max(self.mag)

            # Dump various info to files
            with open(posixpath.join(self.out_path, 'lcs.txt'), 'w') as f:
                f.writelines([' '.join([str(self.lcs[ii][i]) for ii in xrange(len(self.lcs))]) + '\n' for i in xrange(len(self.lcs[0]))])

            with open(posixpath.join(self.out_path, 'track.txt'), 'w') as f:
                for i in xrange(len(self.a_start)):
                    f.write('%s' % datetime.datetime.strftime(self.time[i], '%d.%m.%Y %H:%M:%S.%f'))

                    # a start/end, x start/end, y start/end
                    f.write(' %g %g %g %g %g %g' % (self.a_start[i], self.a_end[i], self.x_start[i], self.x_end[i], self.y_start[i], self.y_end[i]))

                    # Cropped beginning?
                    f.write(' %d' % self.cropped_start[i])
                    # Cropped end?
                    f.write(' %d' % self.cropped_end[i])
                    # Flux
                    f.write(' %g' % self.flux[i])
                    # Mag
                    f.write(' %g' % self.mag[i])

                    f.write('\n')

            if self.a_start[0] < self.a_end[0]:
                self.i_start = self.a_start.index(np.min(self.a_start))
                self.i_end = self.a_end.index(np.max(self.a_end))
            else:
                self.i_start = self.a_start.index(np.max(self.a_start))
                self.i_end = self.a_end.index(np.min(self.a_end))

            self.ang_vel = 0

            if len(self.a_start) > 2:
                for i in xrange(len(self.a_start) - 2):
                    if not self.cropped_end[i] and not self.cropped_end[i + 1]:
                        shift = coord.ang_sep(self.ra_end[i], self.dec_end[i], self.ra_end[i+1], self.dec_end[i+1])
                        self.ang_vel = shift / (self.time[i+1] - self.time[i]).total_seconds()
                        break

            with open(posixpath.join(self.out_path, 'params.txt'), 'w') as f:
                f.write('%d %d %d %d\n' % (self.nframes, self.i_start, self.i_end, self.mag_calibrated))
                f.write('%g %g %g %g\n' % (self.ra_start[self.i_start], self.dec_start[self.i_start], self.ra_end[self.i_end], self.dec_end[self.i_end]))
                f.write('%g\n' % self.ang_vel)
                f.write('%g\n' % self.pixscale)
                f.write('%g %g\n' % (self.rho, self.theta))
                f.write('%g %g %g\n' % (self.mag0, self.mag_min, self.mag_max))
                f.write('%s\n' % self.filter)

            print "Nframes=%d, sd=%g" % (self.nframes, self.sd)
            print "%g, %g -> %g, %g" % (self.ra_start[self.i_start], self.dec_start[self.i_start], self.ra_end[self.i_end], self.dec_end[self.i_end])
            if self.ang_vel:
                print "Angular velocity = %g deg/s" % self.ang_vel

            self.analyzed = True

        self.checkLaser()

        print

    def checkLaser(self):
        image = self.frames[0]['image']

        size = int(0.8*np.max(image.shape))

        thresh = np.percentile(image, 95)
        _,mask = cv2.threshold(image.astype(np.float32), thresh, 1, cv2.THRESH_BINARY)

        pyfits.writeto(posixpath.join(self.out_path, 'mask_laser.fits'), mask.astype(np.uint8), header=self.header, clobber=True)

        # Results are rho, theta: rho = x*cos(theta) + y*sin(theta)
        lines = cv2.HoughLines(mask.astype(np.uint8), 3, np.pi*3/180, size)

        if lines != None and lines.shape[1]:
            self.laser = True
            print "Probably laser event"

    def loadMeteor(self, path=''):
        self.path = path
        self.out_path = posixpath.join(path, 'analysis')

        if not os.path.isdir(path):
            print "Not a directory"
            return

        if not posixpath.exists(posixpath.join(self.out_path, 'median.fits')) or not posixpath.exists(posixpath.join(self.out_path, 'params.txt')):
            self.analyzed = False
            return
        else:
            self.analyzed = True

        self.header = pyfits.getheader(posixpath.join(self.out_path, 'median.fits'), -1)
        self.wcs = pywcs.WCS(self.header)

        with open(posixpath.join(self.out_path, 'params.txt')) as f:
            lines = f.readlines()

            self.nframes, self.i_start, self.i_end, self.mag_calibrated = [int(s) for s in lines[0].split()]
            self.ra_start, self.dec_start, self.ra_end, self.dec_end = [0]*self.nframes, [0]*self.nframes, [0]*self.nframes, [0]*self.nframes
            self.ra_start[self.i_start], self.dec_start[self.i_start], self.ra_end[self.i_end], self.dec_end[self.i_end] = [float(s) for s in lines[1].split()]
            self.ang_vel = float(lines[2])
            self.pixscale = float(lines[3])
            self.rho, self.theta = [float(s) for s in lines[4].split()]
            self.mag0, self.mag_min, self.mag_max = [float(s) for s in lines[5].split()]
            self.filter = lines[6].split()[0]

        with open(posixpath.join(self.out_path, 'lcs.txt')) as f:
            self.lcs = []

            for line in f.readlines():
                s = [float(t) for t in line.split()]
                if not self.lcs:
                    for v in s:
                        self.lcs.append([])
                for i in xrange(len(s)):
                    self.lcs[i].append(s[i])

        with open(posixpath.join(self.out_path, 'track.txt')) as f:
            self.time = []
            self.a_start = []
            self.a_end = []
            self.x_start = []
            self.x_end = []
            self.y_start = []
            self.y_end = []
            self.cropped_start = []
            self.cropped_end = []
            self.flux = []
            self.mag = []

            for line in f.readlines():
                s = line.split()
                self.time.append(datetime.datetime.strptime('%s %s' % (s[0], s[1]), '%d.%m.%Y %H:%M:%S.%f'))
                self.a_start.append(float(s[2]))
                self.a_end.append(float(s[3]))
                self.x_start.append(float(s[4]))
                self.x_end.append(float(s[5]))
                self.y_start.append(float(s[6]))
                self.y_end.append(float(s[7]))
                self.cropped_start.append(int(s[8]))
                self.cropped_end.append(int(s[9]))
                self.flux.append(float(s[10]))
                self.mag.append(float(s[11]))

            self.ra_start,self.dec_start = self.wcs.all_pix2world(self.x_start, self.y_start, 1)
            self.ra_end,self.dec_end = self.wcs.all_pix2world(self.x_end, self.y_end, 1)

        # Zenith distance
        self.favor2.obs.date = self.time[0]
        p = ephem.FixedBody()
        p._ra = self.ra_start[0]*ephem.pi/180.0
        p._dec = self.dec_start[0]*ephem.pi/180.0
        p.compute(self.favor2.obs)
        self.z_start = 90.0 - 180.0*p.alt/ephem.pi

        p._ra = self.ra_end[-1]*ephem.pi/180.0
        p._dec = self.dec_end[-1]*ephem.pi/180.0
        p.compute(self.favor2.obs)
        self.z_end = 90.0 - 180.0*p.alt/ephem.pi

        if posixpath.exists(posixpath.join(self.out_path, 'channels.txt')):
            self.loadChannels()

    def getChannel(self, filter='R'):
        best = None

        for channel in self.channels:
            if channel['filter'] == filter:
                if best is None or channel['a_length'] > best['a_length']:
                    best = channel

        return best

    def analyzeChannels(self):
        channels = []

        for path in [self.path] + glob.glob(posixpath.join(self.path, 'channels', '*_*')):
            channel = {'path': path}
            channels.append(channel)

            out_path = posixpath.join(path, 'analysis')

            print path, out_path

            files = glob.glob(posixpath.join(path, '*_crop.fits'))
            files.sort()

            frames = []
            wcs = None
            has_trail = False

            i = 0
            for filename in files:
                image = pyfits.getdata(filename, -1)
                header = pyfits.getheader(filename, -1)
                time = datetime.datetime.strptime(header['TIME'], '%d.%m.%Y %H:%M:%S.%f')

                if not channel.has_key('channel_id'):
                    channel['id'] = header['CHANNEL ID']
                    channel['filter'] = header['FILTER']

                if i < len(self.time) and np.abs((time - self.time[i]).total_seconds()) < 0.05:
                    has_trail = True
                    i += 1

                frames.append({'image':image, 'time':time, 'header':header, 'filename':filename, 'has_trail':has_trail})

            print "Processing channel %d in filter %s" % (channel['id'], channel['filter'])

            try:
                os.makedirs(out_path)
            except:
                pass

            if posixpath.isfile(posixpath.join(out_path, 'median.fits')):
                print "Loading median"
                median_image = pyfits.getdata(posixpath.join(out_path, 'median.fits'))
            else:
                print "Computing median"
                median_image = np.median([frame['image'] for frame in frames], axis=0)
                pyfits.writeto(posixpath.join(out_path, 'median.fits'), median_image, header=header, clobber=True)

            if posixpath.isfile(posixpath.join(out_path, 'mean0.fits')):
                print "Loading mean0"
                mean0_image = pyfits.getdata(posixpath.join(out_path, 'mean0.fits'))
            else:
                print "Computing mean0"
                mean0_image = np.mean([frame['image'] for frame in frames if not frame['has_trail']], axis=0)
                pyfits.writeto(posixpath.join(out_path, 'mean0.fits'), mean0_image, header=header, clobber=True)

            if posixpath.isfile(posixpath.join(out_path, 'sigma0.fits')):
                print "Loading sigma0"
                sigma0_image = pyfits.getdata(posixpath.join(out_path, 'sigma0.fits'))
            else:
                print "Computing sigma0"
                sigma0_image = np.std([frame['image'] for frame in frames if not frame['has_trail']], axis=0)
                pyfits.writeto(posixpath.join(out_path, 'sigma0.fits'), sigma0_image, header=header, clobber=True)

            channel['times'] = []
            channel['mags'] = []
            channel['mags_err'] = []
            channel['mags_lim'] = []

            ###
            print "Getting WCS"
            wcs = self.favor2.blind_match_file(posixpath.join(out_path, 'mean0.fits'))
            if not wcs:
                wcs = pywcs.WCS(header)

            channel['wcs'] = wcs

            avgname = fix_remote_path(self.favor2.find_image(time, 'avg', channel_id=channel['id']), channel_id=channel['id'])
            darkname = fix_remote_path(self.favor2.find_image(time, 'dark', channel_id=channel['id']), channel_id=channel['id'])

            calib = ss.calibrate_image(avgname, dark=darkname)

            if not calib.has_key('mag0'):
                print "Can't perform photometric calibration! Channel unusable"
                channels.remove(channel)
                continue

            mag0 = calib['mag0']
            channel['mag0'] = mag0

            print "mag0=%g" % mag0

            ### Photometry
            print "Reprojecting coordinates"
            [x1, x2], [y1, y2] = wcs.all_world2pix([self.ra_start[0], self.ra_end[-1]], [self.dec_start[0], self.dec_end[-1]], 0)
            a1, a2 = self.a_start[0], self.a_end[-1]
            dx1, dy1, da = (x2 - x1), (y2 - y1), (a2 - a1)

            if dx1 != 0:
                dx2, dy2 = -dy1/dx1, 1
            else:
                dy2, dx2 = -dx1/dy1, 1

            scale = np.hypot(x2-x1, y2-y1)/(a2-a1)

            dd1 = np.hypot(dx1, dy1)*scale
            dx1, dy1 = dx1/dd1, dy1/dd1

            dd2 = np.hypot(dx2, dy2)*scale
            dx2, dy2 = dx2/dd2, dy2/dd2

            y,x = np.mgrid[0:median_image.shape[0],0:median_image.shape[1]]

            a = (x - x1)*dx1 + (y - y1)*dy1 + a1
            b = (x - x1)*dx2 + (y - y1)*dy2 + 0

            crop_x0, crop_x1, crop_y0, crop_y1 = header['CROP_X0'], header['CROP_X1'], header['CROP_Y0'], header['CROP_Y1']
            idx = (np.abs(b) < 10) & (x + crop_x0 >= 0) & (y + crop_y0 >= 0) & (x + crop_x0 < self.original_width) & (y + crop_y0 < self.original_height)
            channel['a_length'] = np.abs(np.min(a[idx]) - np.max(a[idx])) if len(a[idx]) else 0

            # print (x2 - x1)*dx1 + (y2 - y1)*dy1 + a1, a2

            ###
            print "Performing the photometry"
            i = 0
            for frame in frames:
                if np.abs((frame['time'] - self.time[i]).total_seconds()) < 0.05:
                    image = 1.0*frame['image'] - mean0_image
                    idx = (a >= min(self.a_start[i], self.a_end[i])) & (a <= max(self.a_start[i], self.a_end[i])) & (np.abs(b) < 2.0*10)

                    #limits = np.percentile(image, (0.5, 99.995))
                    #plt.imshow(image, origin='bottom', cmap='hot', interpolation='nearest', vmin=limits[0], vmax=limits[1])
                    #plt.show()

                    flux = np.sum(image[idx])
                    flux_err = np.sqrt(np.sum(sigma0_image[idx]))
                    mag = -2.5*np.log10(flux) + mag0
                    mag_err = 2.5*np.log10(1 + flux_err/np.abs(flux))
                    mag_lim = -2.5*np.log10(3.0*flux_err) + mag0

                    print self.time[i], flux, '+/-', flux_err, mag, '+/-', mag_err, 'limit', mag_lim

                    channel['times'].append(self.time[i])
                    channel['mags'].append(mag)
                    channel['mags_err'].append(mag_err)
                    channel['mags_lim'].append(mag_lim)

                    i += 1

                    if i >= len(self.time):
                        break

            for name in ['times', 'mags', 'mags_err', 'mags_lim']:
                channel[name] = np.array(channel[name])

            with open(posixpath.join(out_path, 'mags.txt'), 'w') as f:
                for i in xrange(len(channel['times'])):
                    f.write('%s' % datetime.datetime.strftime(channel['times'][i], '%d.%m.%Y %H:%M:%S.%f'))
                    f.write(' %g %g %g' % (channel['mags'][i], channel['mags_err'][i], channel['mags_lim'][i]))
                    f.write('\n')

            print

        with open(posixpath.join(self.out_path, 'channels.txt'), 'w') as f:
            for channel in channels:
                f.write('%s %d %s %g\n' % (posixpath.relpath(channel['path'], start=self.path), channel['id'], channel['filter'], channel['a_length']))

        cB = self.getChannel('B')
        cV = self.getChannel('V')
        cR = self.getChannel('R')

        with open(posixpath.join(self.out_path, 'multicolor.txt'), 'w') as f:
            for i in xrange(len(self.time)):
                f.write('%s' % datetime.datetime.strftime(self.time[i], '%d.%m.%Y %H:%M:%S.%f'))

                for c in [cB, cV, cR]:
                    if c is not None:
                        f.write('  %g %g %g' % (c['mags'][i], c['mags_err'][i], c['mags_lim'][i]))
                    else:
                        f.write('  None None None')


        self.channels = channels

    def loadChannels(self):
        if not posixpath.exists(posixpath.join(self.out_path, 'channels.txt')):
            return

        print "Loading channels"

        self.channels = []

        with open(posixpath.join(self.out_path, 'channels.txt')) as f:
            for line in f.readlines():
                s = line.split()
                channel = {'path':posixpath.join(self.path, s[0]), 'id':int(s[1]), 'filter':s[2], 'a_length':float(s[3])}

                self.channels.append(channel)

        for channel in self.channels:
            channel['times'], channel['mags'], channel['mags_err'], channel['mags_lim'] = [], [], [], []

            with open(posixpath.join(channel['path'], 'analysis', 'mags.txt')) as f:
                for line in f.readlines():
                    s = line.split()

                    channel['times'].append(datetime.datetime.strptime('%s %s' % (s[0], s[1]), '%d.%m.%Y %H:%M:%S.%f'))
                    channel['mags'].append(float(s[2]))
                    channel['mags_err'].append(float(s[3]))
                    channel['mags_lim'].append(float(s[4]))

                for name in ['times', 'mags', 'mags_err', 'mags_lim']:
                    channel[name] = np.array(channel[name])

        #print self.channels


# Main
if __name__ == '__main__':
    m = Meteor('ARCHIVE/meteor/2015_08_15/8396781', load=True)
    #m.analyzeChannels()

    pass
