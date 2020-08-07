#!/usr/bin/env python

import os, shutil
import posixpath
import datetime
import tempfile

from favor2 import Favor2
import postprocess
import coadd

from fweb import fitsimage

def mosaic_object(pp, id=0, base=None, size=0):
    path = posixpath.join(base, str(id))

    try:
        os.makedirs(path)
    except:
        pass

    img = pp.favor2.query('SELECT * FROM realtime_images WHERE object_id=%s ORDER BY time ASC;', (id,))

    for i in img:
        time = i['time']
        filename = posixpath.join(path, postprocess.get_filename(time))

        if not posixpath.exists(filename):
            filenames = []

            for id in xrange(1,10):
                raws = pp.find_raw_files(time - datetime.timedelta(seconds=0.05), time + datetime.timedelta(seconds=0.05), channel_id=id)
                if raws:
                    filename1 = raws[0]['fullname']
                    filenames.append(filename1)

            if filenames:
                mosaic = coadd.coadd_mosaic(filenames, clean=True)

                shutil.move(mosaic, filename)

        jpgname = posixpath.splitext(filename)[0] + '.jpg'

        image, xsize, ysize = fitsimage.FitsReadData(filename)
        img = fitsimage.FitsImageFromData(image, xsize, ysize, contrast="percentile", contrast_opts={'max_percent':98.0}, scale="linear")
        if size:
            img.thumbnail((size,size), resample=fitsimage.Image.ANTIALIAS)
        img.save(jpgname, "JPEG", quality=99)

        print filename, jpgname

def mosaic_object_rgb(pp, id=0, base=None, size=0):
    path = posixpath.join(base, str(id))

    try:
        os.makedirs(path)
    except:
        pass

    obj = pp.favor2.query('SELECT * FROM realtime_objects WHERE id=%s;', (id,))

    channels = None # blue, green, red channels
    for i in [[1,2,3],[4,5,6],[7,8,9]]:
        if obj['channel_id'] in i:
            channels = i
            break

    img = pp.favor2.query('SELECT * FROM realtime_images WHERE object_id=%s ORDER BY time ASC;', (id,))

    for i in img:
        time = i['time']
        filename = posixpath.join(path, postprocess.get_filename(time))

        if not posixpath.exists(filename):
            filenames = []

            for id in channels:
                raws = pp.find_raw_files(time - datetime.timedelta(seconds=0.05), time + datetime.timedelta(seconds=0.05), channel_id=id)
                if raws:
                    filename1 = raws[0]['fullname']
                    fd,filename2 = tempfile.mkstemp()
                    os.close(fd)
                    os.system('./crop -clean %s %s' % (filename1, filename2))
                    filenames.append(filename2)

            if filenames:
                jpgname = posixpath.splitext(filename)[0] + '.jpg'

                coadd.coadd_rgb(name_blue=filenames[0], name_green=filenames[1], name_red=filenames[2], out=jpgname)
                for name in filenames:
                    os.unlink(name)

                print filename, jpgname

# Main
if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-i', '--id', help='object ID', action='store', type='int', dest='id', default=0)
    parser.add_option('-b', '--base', help='base path', action='store', type='string', dest='base', default='tmp/mosaic')
    parser.add_option('-s', '--size', help='image size', action='store', type='int', dest='size', default=0)
    parser.add_option('--rgb', help='RGB mode', action='store_true', dest='rgb', default=False)

    (options,args) = parser.parse_args()

    pp = postprocess.postprocessor()

    if options.rgb:
        mosaic_object_rgb(pp, id=options.id, base=options.base, size=options.size)
    else:
        mosaic_object(pp, id=options.id, base=options.base, size=options.size)
