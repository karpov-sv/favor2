#!/usr/bin/env python

import telnetlib, urllib, urllib2
import xml.dom.minidom as minidom
import re
import tempfile
import shutil
import subprocess

class Mount:
    def __init__(self, host='localhost', port=5562):
        print "Connecting to %s:%d" % (host, port)
        self.t = telnetlib.Telnet(host, port)

    def repoint(self, ra=0, dec=0):
        print "Repointing to ra=%g dec=%g" % (ra, dec)
        self.t.write('repoint ra=%g dec=%g\0' % (ra, dec))
        if self.t.expect(['repoint_done', 'repoint_failure', 'repoint_timeout'])[0] == 0:
            print 'Done'
        else:
            print 'Failure'

    def fix(self, ra=0, dec=0):
        print "Fixing current position to ra=%g dec=%g" % (ra, dec)
        self.t.write('fix ra=%g dec=%g\0' % (ra, dec))
        if self.t.expect(['fix_done', 'fix_failure', 'fix_timeout'])[0] == 0:
            print 'Done'
        else:
            print 'Failure'

    def startTracking(self):
        self.t.write('tracking_start\0')
        if self.t.expect(['tracking_start_done', 'tracking_start_failure', 'tracking_start_timeout'])[0] == 0:
            print 'Done'
        else:
            print 'Failure'

    def stopTracking(self):
        self.t.write('tracking_stop\0')
        if self.t.expect(['tracking_stop_done', 'tracking_stop_failure', 'tracking_stop_timeout'])[0] == 0:
            print 'Done'
        else:
            print 'Failure'

    def park(self):
        self.t.write('park\0')
        if self.t.expect(['park_done', 'park_failure', 'park_timeout'])[0] == 0:
            print 'Done'
        else:
            print 'Failure'

def simbadResolve(name = 'm31'):
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

def parseSexadecimal(string):
    value = 0

    m = (re.search("^\s*([+-])?\s*(\d{1,3})\s+(\d{1,2})\s+(\d{1,2}\.?\d*)\s*$", string) or
         re.search("^\s*([+-])?\s*(\d{1,3})\:(\d{1,2})\:(\d{1,2}\.?\d*)\s*$", string))
    if m:
        value = float(m.group(2)) + float(m.group(3))/60 + float(m.group(4))/3600

        if m.group(1) == '-':
            value = -value

    return value

def getFieldCenter(filename):
    """
    Solve the image using local Astrometry.net installation
    """
    odir = tempfile.mkdtemp()

    args = ['./astrometry/bin/solve-field', '-D', odir, '--no-plots', '--no-fits2fits', '--overwrite', '-v', filename]
    ra = -1
    dec = -1

    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    radecline = re.compile('Field center: \(RA H:M:S, Dec D:M:S\) = \(([^,]*),(.*)\).')

    while True:
        a = proc.stdout.readline()
        if a == '':
            break
        match = radecline.match(a)
        if match:
            ra = 15.0*parseSexadecimal(match.group(1))
            dec = parseSexadecimal(match.group(2))

    # cleanup
    shutil.rmtree(odir)

    return ra, dec

def resolve(string = ''):
    """
    Resolve the object name (or coordinates string) into proper coordinates on the sky
    """
    name = ''
    ra = 0
    dec = 0

    m = re.search("^\s*(\d+\.?\d*)\s+([+-]?\d+\.?\d*)\s*$", string)
    if m:
        name = 'degrees'
        ra = float(m.group(1))
        dec = float(m.group(2))
    else:
        m = (re.search("^\s*(\d{1,2})\s+(\d{1,2})\s+(\d{1,2}\.?\d*)\s+([+-])?\s*(\d{1,3})\s+(\d{1,2})\s+(\d{1,2}\.?\d*)\s*$", string) or
             re.search("^\s*(\d{1,2})\:(\d{1,2})\:(\d{1,2}\.?\d*)\s+([+-])?\s*(\d{1,3})\:(\d{1,2})\:(\d{1,2}\.?\d*)\s*$", string))
        if m:
            name = 'sexadecimal'
            ra = (float(m.group(1)) + float(m.group(2))/60 + float(m.group(3))/3600)*15
            dec = (float(m.group(5)) + float(m.group(6))/60 + float(m.group(7))/3600)

            if m.group(4) == '-':
                dec = -dec
        else:
            name, ra, dec = simbadResolve(string)

    return name, ra, dec

if __name__ == '__main__':

    from optparse import OptionParser

    parser = OptionParser(usage="usage: %prog [options] arg")
    parser.add_option('-H', '--host', help='host', action='store', dest='host', default='localhost')
    parser.add_option('-p', '--port', help='port', action='store', dest='port', type='int', default=5562)

    parser.add_option('-r', '--ra', help='RA', action='store', dest='ra', type='float', default=0.0)
    parser.add_option('-d', '--dec', help='Dec', action='store', dest='dec', type='float', default=0.0)

    parser.add_option('-f', '--fix_file', help='Fix the coordinates by image', action='store', dest='fix_file', default='')

    (options,args) = parser.parse_args()

    command = ' '.join(args)

    m = Mount(host=options.host, port=options.port)

    if command == 'start':
        m.startTracking()
    elif command == 'stop':
        m.stopTracking()
    elif command == 'park':
        m.park()
    elif options.fix_file:
        ra, dec = getFieldCenter(options.fix_file)
        if ra >= 0:
            m.fix(ra=ra, dec=dec)
        else:
            print "Error fixing coordinates from %s" % (options.fix_file)
    else:
        if command:
            name,ra,dec = resolve(command)

            if name:
                print "Resolved to %s at ra=%g dec=%g" % (name, ra, dec)
                m.repoint(ra=ra, dec=dec)
            else:
                print "Can't resolve %s" % (command)
        else:
            m.repoint(ra=options.ra, dec=options.dec)
