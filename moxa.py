#!/usr/bin/env python

from multiprocessing import Pool
import telnetlib
import os
import re
import time
import datetime

def tryMoxaReset(hostname):
    try:
        t = telnetlib.Telnet(hostname)
        t.read_until('Key in your selection:')
        t.write("s\r\n")
        t.read_until('Key in your selection:')
        t.write("y\r\n")
        time.sleep(1)
        return True
    except:
        time.sleep(1)
        return False

def waitMoxa(hostname):
    try:
        t = telnetlib.Telnet(hostname)
        t.read_until('Key in your selection:')
        return True
    except:
        time.sleep(1)
        return False

def resetMoxa(hostname, max_time = 30):
    print "Resetting MOXA %s" % hostname
    time_start = datetime.datetime.now()
    
    while not tryMoxaReset(hostname):
        if (datetime.datetime.now() - time_start).total_seconds() > max_time:
            print "Timeout resetting MOXA %s" % hostname
            return 
            
    print "MOXA %s reset" % hostname

    time_start = datetime.datetime.now()

    while not waitMoxa(hostname):
        if (datetime.datetime.now() - time_start).total_seconds() > max_time:
            print "Timeout waiting for MOXA %s" % hostname
            return 

    print "MOXA %s online" % hostname


def resetAllMoxas(hosts = ['192.168.16.134', '192.168.16.135', '192.168.16.136', '192.168.16.137', '192.168.16.138']):
    p = Pool(10)

    p.map(resetMoxa, hosts, 1)
    
if __name__ == '__main__':
    resetAllMoxas()
