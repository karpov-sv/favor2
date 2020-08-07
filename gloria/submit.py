#!/usr/bin/env python

import urllib2, urllib
import xml.dom.minidom as minidom
import json

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

class Gloria:
    base = ''

    def __init__(self, base='http://favor2.info:8880'):
        self.base = base

    def createTarget(self, name, ra, dec, type=""):
        res = urllib2.urlopen(self.base + '/api/create_target?tn=%s&ra=%g&dec=%g&type=%s' % (urllib.quote(name), ra, dec, urllib.quote(type))).read()
        jres = json.loads(res)

        if jres['id'] > 0:
            print "Created the target %d: %s" % (jres['id'], name)

        return jres['id']

    def createTargetFromSimbad(self, name):
        name,ra,dec = simbadResolve(name)

        if name:
            return self.createTarget(name, ra, dec)
        else:
            print "Can't resolve the name %s" % (name)
            return -1

    def deleteTarget(self, id=0):
        res = urllib2.urlopen(self.base + '/api/delete_target?id=%d' % (int(id))).read()
        jres = json.loads(res)

        if jres['ret'] > 0:
            print "Target %d deleted" % (id)

    def deleteAllTargets(self):
        jres = json.loads(urllib2.urlopen(self.base + '/api/list_targets').read())

        for target in jres['targets']:
            print "Deleting target %d: %s" % (target['id'], target['name'])
            self.deleteTarget(target['id'])

    def scheduleTarget(self, id, exposure=50, filter='B'):
        res = urllib2.urlopen(self.base + '/api/schedule?id=%d&exposure=%g&filter=%s' % (id, exposure, filter)).read()
        jres = json.loads(res)

        if jres['id'] > 0:
            print "Target %d scheduled for %g seconds in %s filter: %d" % (id, exposure, filter, jres['id'])

        return jres['id']

    def confirmPlan(self, id):
        res = urllib2.urlopen(self.base + '/api/confirm?id=%d' % (id)).read()
        jres = json.loads(res)

        if jres['ret'] > 0:
            print "Plan %d confirmed" % (id)

    def cancelPlan(self, id):
        res = urllib2.urlopen(self.base + '/api/cancel?id=%d' % (id)).read()
        jres = json.loads(res)

        if jres['ret'] > 0:
            print "Plan %d cancelled" % (id)

    def confirmAllPlans(self):
        jres = json.loads(urllib2.urlopen(self.base + '/api/list?status=approved').read())

        for plan in jres['plans']:
            print "Confirming plan %d: %s for %g seconds in %s filter" % (plan['id'], plan['name'], plan['exposure'], plan['filter'])
            self.confirmPlan(plan['id'])

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-a', '--api', help='base API url, defaults to http://favor2.info:8880', action='store', dest='baseapi', default="http://favor2.info:8880")

    parser.add_option('-n', '--name', help='Target name', action='store', dest='name', default="")
    parser.add_option('--ra', help='Target RA', action='store', dest='ra', type='float', default=0.0)
    parser.add_option('--dec', help='Target Dec', action='store', dest='dec', type='float', default=0.0)

    parser.add_option('-e', '--exposure', help='Exposure', action='store', dest='exposure', type='float', default=50.0)
    parser.add_option('-f', '--filter', help='Filter', action='store', dest='filter', type='string', default="Clear")

    parser.add_option('-i', '--id', help='Target or plan ID', action='store', dest='id', type='int', default=0)

    parser.add_option('-r', '--resolve', help='Resolve object name through SIMBAD', action='store_true', dest='resolve', default=False)

    parser.add_option('-s', '--schedule', help='Schedule the target with given ID for observations', action='store_true', dest='schedule', default=False)
    parser.add_option('-c', '--confirm', help='Confirm the plan with given ID', action='store_true', dest='confirm', default=False)
    parser.add_option('-C', '--confirm-all', help='Confirm all approved plans', action='store_true', dest='confirm_all', default=False)
    parser.add_option('-d', '--delete', help='Delete the target with given ID', action='store_true', dest='delete', default=False)
    parser.add_option('-D', '--delete-all', help='Delete all targets', action='store_true', dest='delete_all', default=False)
    parser.add_option('--cancel', help='Cancel the with given ID', action='store_true', dest='cancel', default=False)

    (options,args) = parser.parse_args()

    gloria = Gloria(options.baseapi)

    if options.delete_all:
        gloria.deleteAllTargets()
    elif options.delete and options.id > 0:
        gloria.deleteTarget(options.id)

    if options.confirm_all:
        gloria.confirmAllPlans()
    elif options.confirm and options.id > 0:
        gloria.confirmPlan(options.id)

    elif options.cancel and options.id > 0:
        gloria.cancelPlan(options.id)

    elif options.schedule and options.id > 0:
        gloria.scheduleTarget(options.id, exposure=options.exposure, filter=options.filter)

    elif options.resolve and options.name:
        gloria.createTargetFromSimbad(options.name)
    elif options.name:
        gloria.createTarget(options.name, options.ra, options.dec)






# for i in [42]:#range(1,111):
#     name = "M%d" % (i)
#     name,ra,dec = simbadResolve(name)
#     target_id = gloria.createTarget(name, ra, dec)
#     plan_id = gloria.scheduleTarget(target_id)
#     #gloria.confirmPlan(plan_id)

#     print name, ra, dec#, target_id, plan_id
