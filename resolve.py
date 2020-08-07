#!/usr/bin/env python

import urllib, urllib2
import xml.dom.minidom as minidom
import re

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

def resolve(string = ''):
    """
    Resolve the object name (or coordinates string) into proper coordinates on the sky
    """
    name = ''
    ra = 0
    dec = 0

    m = re.search("^\s*(\d+\.?\d*)\s*,?\s+([+-]?\d+\.?\d*)\s*$", string)
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
