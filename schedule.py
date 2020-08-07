#!/usr/bin/env python

import telnetlib, urllib, urllib2
import re
import xml.dom.minidom as minidom
import json

def simbadResolve(name):
    web = 'http://cdsweb.u-strasbg.fr/viz-bin/nph-sesame/-oxpi/SNVA?'
    res = urllib2.urlopen(web + urllib.urlencode([('obj', name)]).split('=')[1]).read()

    try:
        xml = minidom.parseString(res)

        r = xml.getElementsByTagName('Resolver')[0]

        name = r.getElementsByTagName('oname')[0].childNodes[0].nodeValue
        ra = float(r.getElementsByTagName('jradeg')[0].childNodes[0].nodeValue)
        dec = float(r.getElementsByTagName('jdedeg')[0].childNodes[0].nodeValue)

        return name, ra, dec
    except:
        return "", 0, 0


class Scheduler:
    def __init__(self, host='localhost', port=5557):
        print "Connecting to %s:%d" % (host, port)
        self.t = telnetlib.Telnet(host, port)

    def scheduleTarget(self, name="", type="", ra=0, dec=0, exposure=0, filter="Clear"):
        print "Scheduling %s (%g %g) for %g seconds in %s filter" % (name, ra, dec, exposure, filter)

        self.t.write('schedule ra=%g dec=%g name=%s type=%s exposure=%g filter=%s\0' % (ra, dec, name, type, exposure, filter))
        if self.t.expect(['schedule_done', 'schedule_failure', 'schedule_timeout'])[0] == 0:
            print 'Done'
        else:
            print 'Failure'

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser(usage="usage: %prog [options] arg")
    parser.add_option('-H', '--host', help='Scheduler host', action='store', dest='host', default='localhost')
    parser.add_option('-p', '--port', help='Scheduler port', action='store', dest='port', type='int', default=5557)

    parser.add_option('-n', '--name', help='Target name', action='store', dest='name', default="noname")
    parser.add_option('-t', '--type', help='Target name', action='store', dest='type', default="imaging")
    parser.add_option('--ra', help='Target RA', action='store', dest='ra', type='float', default=-1.0)
    parser.add_option('--dec', help='Target Dec', action='store', dest='dec', type='float', default=0.0)

    parser.add_option('-e', '--exposure', help='Exposure', action='store', dest='exposure', type='float', default=50.0)
    parser.add_option('-f', '--filter', help='Filter', action='store', dest='filter', type='string', default="Clear")

    parser.add_option('-r', '--resolve', help='Resolve object name through SIMBAD', action='store_true', dest='resolve', default=False)

    (options,args) = parser.parse_args()

    scheduler = Scheduler(options.host, options.port)

    if options.resolve:
        name,ra,dec = simbadResolve(options.name)
        if name:
            scheduler.scheduleTarget(options.name, options.type, ra, dec, options.exposure, options.filter)
    elif options.ra >= 0:
        scheduler.scheduleTarget(options.name, options.type, options.ra, options.dec, options.exposure, options.filter)
