#!/usr/bin/env python

import urllib

def download(ra0=0.0, dec0=0.0, sr0=0.0, filename='apass.tsv'):
    url = "http://vizier.u-strasbg.fr/viz-bin/asu-tsv?-out.all=1&-out.max=unlimited&-out.add=_RAJ2000,_DEJ2000&-source=II/336&-c=%g+%g,rd=%g" % (ra0, dec0, sr0)

    print url

    urllib.urlretrieve(url, filename)

    print "Output written to %s" % filename

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('--ra', help='RA', action='store', dest='ra', type='float', default=0.0)
    parser.add_option('--dec', help='Dec', action='store', dest='dec', type='float', default=0.0)
    parser.add_option('--sr', help='SR', action='store', dest='sr', type='float', default=0.01)
    parser.add_option('--out', help='Filename', action='store', dest='out', default='apass.tsv')

    (options, args) = parser.parse_args()

    download(options.ra, options.dec, options.sr, options.out)
    #print type(options.ra), options.dec, options.sr
