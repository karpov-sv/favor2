#!/usr/bin/env python

from astropy import wcs as pywcs
from astropy.io import fits as pyfits

import numpy as np
import posixpath, glob
import os, sys, datetime

def make_mask(dir, outname='mask.fits', length=0):
    files = glob.glob(posixpath.join(dir, '*.fits'))
    files.sort()

    mask = None
    N = 0

    dx = [-1, -1, -1, 0, 0, 1, 1, 1]
    dy = [-1, 0, 1, -1, 1, -1, 0, 1]

    for filename in files:
        image = pyfits.getdata(filename, -1)

        image = image

        smooth = np.mean([np.roll(np.roll(image, dx[i], 1), dy[i], 0) for i in xrange(len(dx))], axis=0)

        if mask is None:
            mask = np.zeros_like(image)
            mask2 = np.zeros_like(image)

        mask[(image == np.ceil(smooth)) | (image == np.floor(smooth))] += 1

        N += 1

        if length and N >= length:
            break

        sys.stdout.write("\r %d / %d \r" % (N, len(files)))
        sys.stdout.flush()

    omask = np.zeros_like(mask, dtype=np.int16)
    omask[mask>0.5*N] = 0x100

    print "Fraction of masked pixels: %g" % (1.0*len(omask[omask>0])/len(omask.flatten()))

    pyfits.writeto(outname, omask, clobber=True)

    print "Mask written to %s" % outname

# Main
if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-d', '--dir', help='Data dir', action='store', dest='dir', default='')
    parser.add_option('-n', '--length', help='Max length', action='store', type='int', dest='length', default=0)
    parser.add_option('-o', '--out', help='Mask filename, defaults to mask.fits', action='store', dest='out', default='mask.fits')

    (options, args) = parser.parse_args()

    if options.dir:
        make_mask(options.dir, options.out, length=options.length)
