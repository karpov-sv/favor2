#!/usr/bin/env python

from astropy import wcs as pywcs
from astropy.io import fits as pyfits

import numpy as np
import posixpath, glob
import os, sys, datetime
import montage_wrapper as montage
from favor2 import Favor2, fix_remote_path
import secondscale as ss

from calibrate import Calibrator

def make_flats(night='all', imtype='skyflat', flattype='flat', filter='Clear', shutter=-1):
    print "Making flats for night %s" % night

    favor2 = Favor2()
    limit = 0.015

    for channel_id in [1,2,3,4,5,6,7,8,9]:
        res = favor2.query('SELECT * FROM images WHERE type=%s AND channel_id=%s AND filter=get_filter_id(%s) AND (night=%s OR %s=\'all\') AND ((keywords->\'SHUTTER\')::int=%s OR %s<0) ORDER BY time ASC', (imtype, channel_id, filter, night, night, shutter, shutter))
        print "%d flats for channel %d in filter %s" % (len(res), channel_id, filter)

        flats = []
        filenames = []

        calib = None

        for r in res:
            filename = fix_remote_path(r['filename'], channel_id=r['channel_id'])
            flat,header = pyfits.getdata(filename, -1),pyfits.getheader(filename, -1)

            if calib is None:
                calib = Calibrator(shutter=int(r['keywords']['SHUTTER']), channel_id=r['channel_id'])

            flat,header = calib.calibrate(flat,header)
            idx = flat >= header['SATURATE']

            if 'skyflat' in r['type']:
                darkname = favor2.find_image(r['time'], 'dark', channel_id=r['channel_id'])
                darkname = fix_remote_path(darkname, channel_id=r['channel_id'])
                dark = pyfits.getdata(darkname, -1)

                dark = calib.calibrate(dark)

                flat = 1.0*flat - dark

            flat /= np.median(flat)
            flat[flat==0] = 0.1
            flat[idx] = 0.1

            flats.append(flat)
            filenames.append(filename)

        mflat=np.median(flats[:20], axis=0) # Initial estimation of the flat
        mflat1 = None
        N = 0
        flats1 = []

        for i,flat in enumerate(flats):
            frac = flat/mflat

            print filenames[i], np.mean(frac), np.std(frac)

            if np.std(frac) < limit:
                flats1.append(flat)
                if mflat1 is None:
                    mflat1 = flat
                    N = 1
                else:
                    mflat1 += flat
                    N += 1

        print "%d good flats" % N

        if N:
            if N < 20:
                mflat1 = np.median(flats1, axis=0)
            else:
                mflat1 /= N

            r = res[0]
            time = r['time']
            header = pyfits.getheader(filenames[0], -1)
            header['TYPE'] = flattype

            if posixpath.exists(posixpath.join('AVG', r['night'])):
                filename = posixpath.join('AVG', r['night'], time.strftime('%Y%m%d_%H%M%S')+'_%d.%s.fits' % (r['channel_id'], flattype))
            else:
                filename = posixpath.join('/MMT/%d' % r['channel_id'], 'AVG', r['night'], time.strftime('%Y%m%d_%H%M%S')+'_%d.%s.fits' % (r['channel_id'], flattype))

            print filename
            pyfits.writeto(filename, mflat1, header, clobber=True)
            favor2.register_image(filename, imtype=flattype, time=time)

# Main
if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-n', '--night', help='Night to process, defaults to all', action='store', dest='night', default='all')
    parser.add_option('-i', '--imtype', help='Image type to process, defaults to skyflat', action='store', dest='imtype', default='skyflat')
    parser.add_option('-t', '--flattype', help='Flat type, defaults to flat', action='store', dest='flattype', default='flat')
    parser.add_option('-f', '--filter', help='Filter, defaults to Clear', action='store', dest='filter', default='Clear')
    parser.add_option('-s', '--shutter', help='Shutter, defaults to all', action='store', dest='shutter', default=-1)

    (options, args) = parser.parse_args()

    if options.night:
        make_flats(night=options.night, imtype=options.imtype, flattype=options.flattype, filter=options.filter, shutter=options.shutter)
