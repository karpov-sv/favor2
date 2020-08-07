#!/usr/bin/env python

import posixpath
import os
import re
import shutil
import tempfile
import pyfits

from aplpy import make_rgb_image
#from aplpy import make_rgb_cube
from coadd_rgb import make_rgb_cube
from favor2 import Favor2

import montage_wrapper as montage

def blind_match(filename, outname=None):
    # dir = tempfile.mkdtemp(prefix='match')
    # wcs = None
    # binname = None
    f = Favor2()
    
    if not outname:
        # outname = posixpath.splitext(filename)[0] + '.new'
        # if outname == filename:
        #     outname = filename + '.new'
        fd,outname = tempfile.mkstemp(prefix='match', suffix='.new')
        os.close(fd)

    f.blind_match_file(filename, outfile=outname)
        
    # for path in ['.', '/usr/local', '/opt/local']:
    #     if os.path.isfile(posixpath.join(path, 'astrometry', 'bin', 'solve-field')):
    #         binname = posixpath.join(path, 'astrometry', 'bin', 'solve-field')
    #         break

    # if binname:
    #     command = "%s -D %s --overwrite --no-fits2fits --no-plots --no-verify --use-sextractor -t 2 %s --new-fits %s"  % (binname, dir, filename, outname)

    #     #os.system("%s  >/dev/null 2>/dev/null" % (command))
    #     os.system("%s" % (command))

    # shutil.rmtree(dir, ignore_errors=True)

    if not posixpath.isfile(outname):
        outname = None

    return outname

def make_temp_file(filename):
    if filename is None:
        return None

    image = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    fd, newname = tempfile.mkstemp(prefix='tmp_', suffix='.fits')
    os.close(fd)

    pyfits.writeto(newname, image, header, clobber=True)

    return newname

def fix_fits_crop(filename):
    if filename is None:
        return None

    image = pyfits.getdata(filename, -1)
    header = pyfits.getheader(filename, -1)

    if not header.get('CROP_X1') or not header.get('CROP_Y1'):
        return
        
    x0, y0, x1, y1 = header['CROP_X0'], header['CROP_Y0'], header['CROP_X1'], header['CROP_Y1']
    width0, height0 = 2560, 2160

    # FIXME: we need better heuristic!
    if header.get('II_POWER'):
        width0, height0 = 1366, 1036
    
    xc0 = max(0, -x0)
    yc0 = max(0, -y0)
    
    xc1 = min(x1-x0, width0-x0)
    yc1 = min(y1-y0, height0-y0)
    
    #print '-> ', xc0, yc0, xc1, yc1
    
    header['CRPIX1'] -= xc0
    header['CRPIX2'] -= yc0
    
    image = image[yc0:yc1, xc0:xc1]
    
    pyfits.writeto(filename, image, header, clobber=True)
    
def coadd_rgb(name_red=None, name_green=None, name_blue=None, out=None, pmin=0.02, pmax=0.98, ra0=None, dec0=None, sr0=None, north=False, match=True):
    if ((not name_red or not posixpath.isfile(name_red)) and
        (not name_green or not posixpath.isfile(name_green)) and
        (not name_blue or not posixpath.isfile(name_blue))):

        print "Not enough frames to make three-color image!"
        return

    if not out:
        out = "rgb.jpg"

    fd,cube = tempfile.mkstemp(prefix='cube', suffix='.fits')
    os.close(fd)

    print "Co-adding three-color image: red=%s green=%s blue=%s" % (name_red, name_green, name_blue)

    if match:
        matched_red = blind_match(name_red)
        matched_green = blind_match(name_green)
        matched_blue = blind_match(name_blue)
    else:
        matched_red = make_temp_file(name_red)
        matched_green = make_temp_file(name_green)
        matched_blue = make_temp_file(name_blue)

    fix_fits_crop(matched_red)
    fix_fits_crop(matched_green)
    fix_fits_crop(matched_blue)
        
    if ra0 or dec0 or sr0:
        import montage_wrapper as montage
        montage.mSubimage(matched_red, matched_red, ra0, dec0, sr0)
        montage.mSubimage(matched_green, matched_green, ra0, dec0, sr0)
        montage.mSubimage(matched_blue, matched_blue, ra0, dec0, sr0)

    if match and (not matched_red or not matched_green or not matched_blue):
        print "Can't match input frames"
        return None

    make_rgb_cube([matched_red, matched_green, matched_blue], cube, north=north)
    make_rgb_image(cube, out, embed_avm_tags=False, make_nans_transparent=True,
                   pmin_r=pmin*100, pmin_g=pmin*100, pmin_b=pmin*100,
                   pmax_r=pmax*100, pmax_g=pmax*100, pmax_b=pmax*100)

    os.unlink(cube)

    if matched_red:
        os.unlink(matched_red)
    if matched_green:
        os.unlink(matched_green)
    if matched_blue:
        os.unlink(matched_blue)

    if posixpath.isfile(out):
        print "Successfully created RGB image %s" % out
    else:
        out = None

    return out

def coadd_mosaic(filenames=[], as_string=False, match=True, clean=False):
    indir = tempfile.mkdtemp('_mosaic_in')
    outdir = tempfile.mkdtemp('_mosaic_out')

    out = None

    id = 0
    for file in filenames:
        # Make filenames more unique
        newname = posixpath.split(file)[-1]
        newname = '%d_%s' % (id, newname)
        newname = posixpath.join(indir, newname)

        print "Matching file %s" % file

        command = "./crop"
        if match:
            command = command + " -match"
        if clean:
            command = command + " -clean"

        os.system("%s %s %s" % (command, file, newname))
        #blind_match(file, outname=newname)

        id = id + 1

    print "Running Montage"

    # Montage will re-create the dir
    shutil.rmtree(outdir, ignore_errors=True)
    montage.mosaic(indir, outdir)

    mosaic_name = posixpath.join(outdir, 'mosaic.fits')

    if posixpath.exists(mosaic_name):
        if as_string:
            with open(mosaic_name, 'r') as file:
                out = file.read()
        else:
            fd,outname = tempfile.mkstemp(prefix='mosaic_', suffix='.fits')
            os.close(fd)

            shutil.copy(mosaic_name, outname)

            out = outname

    shutil.rmtree(indir, ignore_errors=True)
    shutil.rmtree(outdir, ignore_errors=True)

    print "Mosaicking finished"

    return out

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-r', '--red', help='Red channel image', action='store', dest='red', default=None)
    parser.add_option('-g', '--green', help='Green channel image', action='store', dest='green', default=None)
    parser.add_option('-b', '--blue', help='Blue channel image', action='store', dest='blue', default=None)
    parser.add_option('-o', '--out', help='Output RGB image', action='store', dest='out', default=None)
    parser.add_option('-m', '--pmax', help='Upper quantile', action='store', type='float', dest='pmax', default=0.98)
    parser.add_option('-M', '--pmin', help='Lower quantile', action='store', type='float', dest='pmin', default=0.02)
    parser.add_option('--nomatch', help='Do not match coordinates', action='store_true', dest='is_nomatch', default=False)

    (options,args) = parser.parse_args()

    coadd_rgb(name_red=options.red, name_green=options.green, name_blue=options.blue, out=options.out,
              pmin=options.pmin, pmax=options.pmax, match=not options.is_nomatch)
