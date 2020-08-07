#!/usr/bin/env python

# pygcn - may be installed as: pip install pygcn
# https://github.com/lpsinger/pygcn
import gcn
import gcn.notice_types as n

import datetime
import logging
import telnetlib
import posixpath, shutil, os, glob
import requests
from astropy.io import fits as pyfits
from astropy import wcs as pywcs
from favor2 import Favor2, send_email, fix_remote_path
from webchannel_plots import beholder_plot_status
from coord import ang_sep

logging.basicConfig(level=logging.WARNING)

scheduler_host = 'localhost'
scheduler_port = 5557

def message_to_scheduler(message):
    try:
        t = telnetlib.Telnet(scheduler_host, scheduler_port)
        t.write('%s\0' % message)
        t.close()
    except:
       pass

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from mpl_toolkits.basemap import Basemap
import healpy as hp
import numpy as np

def plot_LVC_map(id=0, size=800, favor2=None, horizon=True, file=None):
    favor2 = favor2 or Favor2()

    target = favor2.query("SELECT * FROM scheduler_targets WHERE id=%s", (id, ), simplify=True)
    images = favor2.query("SELECT * FROM images WHERE keywords->'TARGET ID' = %s::text ORDER BY time", (target['id'],), simplify=False)

    # Trigger base dir
    base = posixpath.join('TRIGGERS', target['type'], str(target['external_id']))
    files = glob.glob(posixpath.join(base, '*.fits.gz'))
    files.sort(reverse=True)

    skymap, header = hp.read_map(files[0], h=True, verbose=False)
    header = dict(header)
    date = header['DATE'][:19]
    date = datetime.datetime.strptime(date, '%Y-%m-%dT%H:%M:%S')
    # TODO: use time from file header
    date = target['time_created']

    fig = Figure(facecolor='white', dpi=72, figsize=(size/72, 0.6*size/72), tight_layout=True)
    ax = fig.add_subplot(111)
    # set up gnomonic map projection
    map = Basemap(projection='hammer', lat_0 = 0, lon_0 = 0, resolution = None, celestial = True, ax = ax)
    # draw the edge of the map projection region (the projection limb)
    map.drawmapboundary()
    degtoralabel = lambda deg : "$%d^h$" % int(deg/15)
    degtodeclabel = lambda deg : "%+d$^\circ$" % deg
    map.drawparallels(np.arange(-90, 90, 30), color='lightgray')#, fmt=degtodeclabel)
    map.drawmeridians(np.arange(0, 360, 45), color='lightgray')

    for h in [0,3,6,9,12,15,18,21]:
        try:
            x,y = map(h*15,0)
            if abs(x) < 1e10 and abs(y) < 1e10:
                ax.text(x, y, degtoralabel(h*15), color='black')
        except:
            pass


    # Rebin the map
    nside = hp.npix2nside(len(skymap))
    ra=np.arange(0, 360, 1)
    dec=np.arange(-90, 90, 1)
    theta, phi = np.deg2rad(90 - dec), np.deg2rad(ra)
    PHI, THETA = np.meshgrid(phi, theta)
    grid_pix = hp.ang2pix(nside, THETA, PHI)
    grid_map = skymap[grid_pix]

    ra0, dec0 = np.meshgrid(ra, dec)
    m = map.pcolormesh(ra0, dec0, grid_map, latlon=True, cmap='YlOrRd')
    #map.colorbar(m)

    for image in images:
        filename = fix_remote_path(image['filename'], channel_id=image['channel_id'])
        header = pyfits.getheader(filename, -1)
        wcs = pywcs.WCS(header)
        ra1, dec1 = wcs.all_pix2world([0, 0, header['NAXIS1'], header['NAXIS1'], 0], [0, header['NAXIS2'], header['NAXIS2'], 0, 0], 0)
        try:
            map.plot(ra1, dec1, '-', latlon=True, color='blue')
        except:
            pass

    fig.suptitle("Trigger %s at %s" % (target['name'], date))
    if images:
        ax.set_title("%d images from %s to %s" % (len(images), images[0]['time'], images[-1]['time']))

    if horizon:
        for iter in [0,1]:
            if iter == 0:
                if images:
                    favor2.obs.date = images[0]['time']
                else:
                    favor2.obs.date = date
                color, alpha = 'black', 1
            else:
                favor2.obs.date = datetime.datetime.utcnow()
                color, alpha = 'black', 0.3

            ra_h, dec_h = [], []
            for A in np.arange(0, 2*np.pi, 0.1):
                radec = favor2.obs.radec_of(A, np.deg2rad(0))
                ra_h.append(np.rad2deg(radec[0]))
                dec_h.append(np.rad2deg(radec[1]))

            map.plot(ra_h, dec_h, '--', latlon=True, color=color, alpha=alpha)

        favor2.obs.date = date
        # Sun
        s = favor2.sun
        s.compute(favor2.obs)
        px, py = map(np.rad2deg(s.ra), np.rad2deg(s.dec))
        if abs(px) < 1e10 and abs(py) < 1e10:
            map.scatter(px, py, s=2000, c='lightgray', marker='o', alpha=0.7)
            ax.text(px, py, "Sun", color='black', va='center', ha='center')

        # Moon
        m = favor2.moon
        m.compute(favor2.obs)
        px, py = map(np.rad2deg(m.ra), np.rad2deg(m.dec))
        if abs(px) < 1e10 and abs(py) < 1e10:
            map.scatter(px, py, s=1200, c='lightgray', marker='o', alpha=0.7)
            ax.text(px, py, "Moon", color='black', va='center', ha='center')

    # Return the image
    if file:
        canvas = FigureCanvas(fig)
        canvas.print_png(file, bbox_inches='tight')

def gcn_handler(payload, root):
    type = int(root.find("./What/Param[@name='Packet_Type']").attrib['value'])
    notice_date = root.find("./Who/Date").text.strip()
    # We are interested in Swift packets, as described at http://gcn.gsfc.nasa.gov/swift.html

    if type == n.SWIFT_BAT_GRB_POS_ACK: # or type == n.SWIFT_BAT_GRB_POS_TEST:
        description = root.find("./What/Description").text.strip()
        trigID = int(root.find("./What/Param[@name='TrigID']").attrib['value'])
        is_GRB = root.find("./What/Group[@name='Solution_Status']/Param[@name='GRB_Identified']").attrib['value']
        is_not_GRB = root.find("./What/Group[@name='Solution_Status']/Param[@name='Def_NOT_a_GRB']").attrib['value']

        time = root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords/Time/TimeInstant/ISOTime").text.strip()
        ra = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C1").text)
        dec = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C2").text)
        err = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Error2Radius").text)

        time = datetime.datetime.strptime(time, "%Y-%m-%dT%H:%M:%S.%f")

        if is_GRB == 'true' and is_not_GRB == 'false':
            # TODO: Some weak bursts or bursts in nearby galaxies may actually have is_GRB=false
            print
            print "Swift BAT GRB Position message"
            print description
            print "trigger ID=%d RA=%.2f Dec=%.2f, err=%.2g at %s" % (trigID, ra, dec, err, time)

            dirname = posixpath.join(base_path, "Swift", str(trigID))
            filename = posixpath.join(dirname, "voevent_%d.xml" % (trigID,))

            # Make event dir
            try:
                os.makedirs(dirname)
            except:
                pass

            # Store the message
            with open(filename, "w") as f:
                f.write(payload)

            # Here we may send the coordinates to the scheduler, if the accuracy is good enough
            if not favor2.query('SELECT * FROM scheduler_targets WHERE external_id=%s AND type=%s', (trigID, 'Swift')):
                id = favor2.query("INSERT INTO scheduler_targets (external_id, ra, dec, name, type, time_created, timetolive, priority, params) VALUES (%s, %s, %s, %s, %s, %s, %s, 100, 'error=>%s'::hstore) RETURNING id", (trigID, ra, dec, 'Swift_%d' % trigID, 'Swift', time, 1.0*3600, err), simplify=True)
                # We should also inform the scheduler that new high-priority target has been added
                message_to_scheduler('reschedule')

                subject = 'GCN: BAT trigger %d with RA=%.2f Dec=%.2f at %s' % (trigID, ra, dec, time)
                body = subject
                body += "\n"
                body += "http://gcn.gsfc.nasa.gov/other/%d.swift\n" % (trigID)
                body += "http://mmt.favor2.info/scheduler/%d" % (id)

                statusname = "/tmp/status.png"
                beholder_plot_status(file=statusname,
                                     extra=[{'type':'circle', 'ra':ra, 'dec':dec, 'sr':1, 'color':'darkcyan', 'lw':3}])

                send_email(body, subject=subject, attachments=[statusname, filename])
                send_email(body, to='gbeskin@mail.ru', subject=subject, attachments=[statusname, filename])

            print ""

        else:
            print "BAT Position trigger, but not a GRB"
            print ""

    elif type == n.SWIFT_XRT_POSITION:
        description = root.find("./What/Description").text.strip()
        trigID = int(root.find("./What/Param[@name='TrigID']").attrib['value'])
        is_not_GRB = root.find("./What/Group[@name='Solution_Status']/Param[@name='Def_NOT_a_GRB']").attrib['value']

        time = root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords/Time/TimeInstant/ISOTime").text.strip()
        ra = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C1").text)
        dec = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C2").text)
        err = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Error2Radius").text)

        time = datetime.datetime.strptime(time, "%Y-%m-%dT%H:%M:%S.%f")

        if is_not_GRB == 'false':
            # TODO: Probably we should check whether ID matches the one of previously arrived GRB BAT position
            print
            print "Swift XRT GRB position message"
            print description
            print "trigger ID=%s RA=%.2f Dec=%.2f, err=%.2g at %s" % (trigID, ra, dec, err, time)
            print ""

            # Try to update the coordinates for previous BAT trigger target
            favor2.query("UPDATE scheduler_targets SET ra=%s, dec=%s, params='error=>%s'::hstore WHERE external_id=%s AND type=%s", (ra, dec, err, trigID, 'Swift'))

        else:
            print "XRT Position trigger, but not a GRB"
            print ""

    elif type == n.FERMI_GBM_ALERT:
        description = root.find("./What/Description").text.strip()
        trigID = int(root.find("./What/Param[@name='TrigID']").attrib['value'])
        time = root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords/Time/TimeInstant/ISOTime").text.strip()
        time = datetime.datetime.strptime(time, "%Y-%m-%dT%H:%M:%S.%f")

        print
        print "Fermi GBM GRB Alert"
        print description
        print "trigger ID=%d at %s" % (trigID, time)

    elif type == n.FERMI_GBM_FLT_POS or type == n.FERMI_GBM_GND_POS:
        description = root.find("./What/Description").text.strip()
        trigID = int(root.find("./What/Param[@name='TrigID']").attrib['value'])
        is_not_GRB = root.find("./What/Group[@name='Trigger_ID']/Param[@name='Def_NOT_a_GRB']").attrib['value']

        time = root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords/Time/TimeInstant/ISOTime").text.strip()
        ra = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C1").text)
        dec = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C2").text)
        err = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Error2Radius").text)

        time = datetime.datetime.strptime(time, "%Y-%m-%dT%H:%M:%S.%f")

        if is_not_GRB == 'false':
            # TODO: Some weak bursts or bursts in nearby galaxies may actually have is_GRB=false
            print
            print "Fermi GBM GRB Position message"
            print description
            print "trigger ID=%d RA=%.2f Dec=%.2f, err=%.2g at %s" % (trigID, ra, dec, err, time)

            if type == n.FERMI_GBM_FLT_POS:
                # Only Flight Pos has this classification
                mostlikely_id = int(root.find("./What/Param[@name='Most_Likely_Index']").attrib['value'])
                mostlikely_prob = float(root.find("./What/Param[@name='Most_Likely_Prob']").attrib['value'])

                mostlikely_str = {0: "ERROR",
                                  1: "UNRELIABLE_LOCATION",
                                  2: "LOCAL_PARTICLES",
                                  3: "BELOW_HORIZON",
                                  4: "GRB",
                                  5: "GENERIC_SGR",
                                  6: "GENERIC_TRANSIENT",
                                  7: "DISTANT_PARTICLES",
                                  8: "SOLAR_FLARE",
                                  9: "CYG_X1",
                                  10: "SGR_1806_20",
                                  11: "GROJ_0422_32",
                                  19: "TGF",
                                  100: "UNDEF"
                }.get(mostlikely_id, 100)

                print "In-Flight Classification: %s (%d) with prob %g%%" % (mostlikely_str, mostlikely_id, mostlikely_prob)

                if mostlikely_id < 4 or mostlikely_id > 6:
                    print "Skipping"
                    return

            dirname = posixpath.join(base_path, "Fermi", str(trigID))
            filename = posixpath.join(dirname, "voevent_%d_%s.xml" % (trigID, notice_date))

            # Make event dir
            try:
                os.makedirs(dirname)
            except:
                pass

            # Store the message
            with open(filename, "w") as f:
                f.write(payload)

            # Previous entry on that trigger, if any
            target = favor2.query('SELECT * FROM scheduler_targets WHERE external_id=%s AND type=%s LIMIT 1', (trigID, 'Fermi'), simplify=True)

            # Here we may send the coordinates to the scheduler, if the accuracy is good enough
            if not target:
                id = favor2.query("INSERT INTO scheduler_targets (external_id, ra, dec, name, type, time_created, timetolive, priority, params) VALUES (%s, %s, %s, %s, %s, %s, %s, 100, 'error=>%s'::hstore) RETURNING id", (trigID, ra, dec, 'Fermi_%d' % trigID, 'Fermi', time, 1.0*3600, err), simplify=True)
                # We should also inform the scheduler that new high-priority target has been added
                message_to_scheduler('reschedule')

                subject = 'GCN: GBM trigger %d with RA=%.2f Dec=%.2f err=%.2g at %s' % (trigID, ra, dec, err, time)
                body = subject
                body += "\n"
                body += "http://gcn.gsfc.nasa.gov/other/%d.fermi\n" % (trigID)
                body += "http://mmt.favor2.info/scheduler/%d" % (id)

                statusname = "/tmp/status.png"
                beholder_plot_status(file=statusname,
                                     extra=[{'type':'circle', 'ra':ra, 'dec':dec, 'sr':max(1,err), 'color':'darkcyan', 'lw':3}])

                send_email(body, subject=subject, attachments=[statusname, filename])
                send_email(body, to='gbeskin@mail.ru', subject=subject, attachments=[statusname, filename])

            elif target['params'] and float(target['params'].get('error')) > err:
                # Just update the coordinates
                favor2.query("UPDATE scheduler_targets SET ra=%s, dec=%s, params='error=>%s'::hstore WHERE external_id=%s AND type=%s", (ra, dec, err, trigID, 'Fermi'))

                if ang_sep(ra, dec, target['ra'], target['dec']) > float(target['params'].get('error')):
                    # Coordinates changed significantly, let's inform the scheduler
                    message_to_scheduler('reschedule')

            print ""

        else:
            print "GBM Position trigger, but not a GRB"
            print ""

    elif type == n.SWIFT_POINTDIR:
        description = root.find("./What/Description").text.strip()

        time = root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords/Time/TimeInstant/ISOTime").text.strip()
        time = datetime.datetime.strptime(time, "%Y-%m-%dT%H:%M:%S.%f")

        ra = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C1").text)
        dec = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C2").text)

        print "Swift pointing notice"
        print description
        print "RA=%.2f Dec=%.2f at %s" % (ra, dec, time)

        dt = (time - datetime.datetime.utcnow()).total_seconds()

        if abs(dt) < 600:
            print "Repointing in %g seconds from now, will inform scheduler" % dt
            message_to_scheduler('add_interest name=Swift ra=%g dec=%g radius=40 weight=10 reschedule=0' % (ra, dec))

    elif type == n.FERMI_POINTDIR:
        description = root.find("./What/Description").text.strip()

        time = root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords/Time/TimeInstant/ISOTime").text.strip()
        time = datetime.datetime.strptime(time, "%Y-%m-%dT%H:%M:%S.%f")

        ra = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C1").text)
        dec = float(root.find("./WhereWhen/ObsDataLocation/ObservationLocation/AstroCoords[@coord_system_id='UTC-FK5-GEO']/Position2D[@unit='deg']/Value2/C2").text)

        print "Fermi pointing notice"
        print description
        print "RA=%.2f Dec=%.2f at %s" % (ra, dec, time)

        dt = (time - datetime.datetime.utcnow()).total_seconds()

        if abs(dt) < 600:
            print "Repointing in %g seconds from now, will inform scheduler" % dt
            message_to_scheduler('add_interest name=Fermi ra=%g dec=%g radius=30 weight=5 reschedule=0' % (ra, dec))

    elif type == n.LVC_INITIAL or type == n.LVC_UPDATE:
        print "LVC notice, role=%s at %s" % (root.attrib['role'], root.find("./Who/Date").text)

        # Respond only to 'test' events.
        # VERY IMPORTANT! Replace with the following line of code
        # to respond to only real 'observation' events.
        if root.attrib['role'] != 'observation': return
        # if root.attrib['role'] != 'test': return

        # Respond only to 'CBC' events. Change 'CBC' to "Burst' to respond to only
        # unmodeled burst events.
        # if root.find("./What/Param[@name='Group']").attrib['value'] != 'CBC': return

        print
        print "Processing LVC notice"

        id = int(root.find("./What/Param[@name='TrigID']").attrib['value'])
        alert_type = root.find("./What/Param[@name='AlertType']").attrib['value']
        date = root.find("./Who/Date").text

        name = 'LVC_%d' % id
        if root.attrib['role'] == 'test':
            name = name + '_test'

        print "Trigger %d at %s - %s" % (id, date, alert_type)

        dirname = posixpath.join(base_path, "LVC", str(id))
        filename = posixpath.join(dirname, "voevent_%d_%s.xml" % (id, alert_type))

        # test messages should not go to the scheduler too often
        if root.attrib['role'] != 'test' or favor2.query("SELECT count(*) FROM scheduler_targets WHERE type='LVC' AND status=get_scheduler_target_status_id('active') AND name LIKE '%_test'", simplify=True) == 0 or favor2.query('SELECT * FROM scheduler_targets WHERE external_id=%s AND type=%s', (id, 'LVC')):
            # Make event dir
            try:
                os.makedirs(dirname)
            except:
                pass

            # Store the message
            with open(filename, "w") as f:
                f.write(payload)

            skymap_url = root.find("./What/Param[@name='SKYMAP_URL_FITS_BASIC']").attrib['value']

            #mapname = posixpath.join(dirname, "map_%d_%s.fits" % (id, alert_type))
            mapname = posixpath.join(dirname, posixpath.split(skymap_url)[-1])

            if not posixpath.exists(mapname) or True:
                # Download the map
                response = requests.get(skymap_url, stream=True, auth=(gracedb_username, gracedb_password))
                response.raise_for_status()

                # Store the map
                with open(mapname, "w") as f:
                    shutil.copyfileobj(response.raw, f)
                    f.flush()
                    print "Map saved to %s" % mapname

            if not favor2.query('SELECT * FROM scheduler_targets WHERE external_id=%s AND type=%s', (id, 'LVC')):
                tid = favor2.query("INSERT INTO scheduler_targets (external_id, name, type, timetolive, priority) VALUES (%s, %s, %s, %s, 10) RETURNING id", (id, name, 'LVC', 24*3600), simplify=True)
                # We should also inform the scheduler that new high-priority target has been added
                message_to_scheduler('reschedule')

                imagename = posixpath.join(dirname, "map.png")
                statusname = posixpath.join(dirname, "status.png")
                plot_LVC_map(tid, file=imagename)
                beholder_plot_status(file=statusname)

                subject = 'GCN: LVC trigger %d / %s at %s' % (id, alert_type, date)
                body = subject

                send_email(body, subject=subject, attachments=[imagename, statusname, filename])
                send_email(body, to='gbeskin@mail.ru', subject=subject, attachments=[statusname, filename])

        else:
            print "skipped"

        print

    else:
        print "Unhandled message: %d" % type
        pass

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser(usage="usage: %prog [options] arg")
    parser.add_option('-b', '--base', help='Base path for storing data', action='store', dest='base_path', default='TRIGGERS')
    parser.add_option('-H', '--host', help='Host to connect', action='store', dest='host', default='68.169.57.253')
    parser.add_option('-p', '--port', help='Port to connect', action='store', dest='port', type='int', default=8099)
    parser.add_option('--scheduler-host', help='Hostname of scheduler', action='store', dest='scheduler_host', default='localhost')
    parser.add_option('--scheduler-port', help='Port number of scheduler', action='store', dest='scheduler_port', type='int', default=5557)

    parser.add_option('-f', '--fake', help='Initiate fake trigger', action='store_true', dest='is_fake', default=False)
    parser.add_option('--ra', help='RA', action='store', dest='ra', type='float', default=0.0)
    parser.add_option('--dec', help='Dec', action='store', dest='dec', type='float', default=0.0)

    (options,args) = parser.parse_args()

    scheduler_host = options.scheduler_host
    scheduler_port = options.scheduler_port
    base_path = options.base_path
    favor2 = Favor2()

    # TODO: make it configurable
    gracedb_username='106083321185023610598@google.com'
    gracedb_password='w588MNtaxcUuJLhnmdSq'

    if options.is_fake:
        print "Initiating fake trigger at RA=%g Dec=%g" % (options.ra, options.dec)
        favor2.query("INSERT INTO scheduler_targets (ra, dec, name, type, timetolive, priority) VALUES (%s, %s, %s, %s, %s, 10) RETURNING id", (options.ra, options.dec, 'test', 'Swift', 3600), simplify=True)
        # We should also inform the scheduler that new high-priority target has been added
        message_to_scheduler('reschedule')

    else:
        print "Listening for VOEvents at %s:%d" % (options.host, options.port)
        print "Serving to scheduler at %s:%d" % (scheduler_host, scheduler_port)

        gcn.listen(host=options.host, port=options.port, handler=gcn_handler)
