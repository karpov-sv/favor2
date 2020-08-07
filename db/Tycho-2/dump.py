#!/usr/bin/env python

import os, sys, posixpath, glob
import numpy as np

def process_file(filename):
    with open(filename) as file:
        while True:
            line = file.readline()

            if not line:
                break

            s = line.split('|')

            name = s[0]

            if s[1] == 'X':
                ra = float(s[24])
                dec = float(s[25])
            else:
                ra = float(s[2])
                dec = float(s[3])

            p_ra = float(s[4].strip() or 0)
            p_dec = float(s[5].strip() or 0)

            deltat = 16.0

            ra1 = (ra + (1e-3*p_ra/3600.0)*deltat/np.cos(np.deg2rad(dec))) % 360.0
            dec1 = dec + (1e-3*p_dec/3600.0)*deltat

            if dec1 > 90:
                dec1 = 180.0 - dec1
                ra1 = (180.0 + ra1) % 360.0

            if dec1 < -90:
                dec1 = -180.0 - dec1
                ra1 = (180.0 + ra1) % 360.0

            if 'suppl' in filename:
                Bt = float(s[11].strip() or 0)
                eBt = float(s[12].strip() or 0)
                Vt = float(s[13].strip() or 0)
                eVt = float(s[14].strip() or 0)
            else:
                Bt = float(s[17].strip() or 0)
                eBt = float(s[18].strip() or 0)
                Vt = float(s[19].strip() or 0)
                eVt = float(s[20].strip() or 0)

            if Bt == 0 and Vt != 0:
                Bt = Vt
            if Vt == 0 and Bt != 0:
                Vt = Bt

            print "%s\t%f\t%f\t%f\t%f\t%f\t%f" % (name, ra1, dec1, Bt, Vt, eBt, eVt)

if __name__ == '__main__':
    for filename in ['catalog.dat', 'suppl_1.dat', 'suppl_2.dat']:
        process_file('data/' + filename)
