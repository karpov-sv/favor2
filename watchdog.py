#!/usr/bin/env python

from twisted.internet.protocol import Protocol, ReconnectingClientFactory
from twisted.internet.task import LoopingCall

from webchannel import Favor2Protocol, Favor2ClientFactory

import os
import json
import datetime
from gui import immp

from favor2 import send_email

def breakpoint():
    import pdb
    pdb.set_trace()

class BeholderProtocol(Favor2Protocol):
    refresh = 1

    def requestStatus(self):
        #print "requesting status"
        self.message("get_beholder_status")

    def processMessage(self, string):
        command = immp.parse(string)

        if command.name() == 'beholder_status':
            self.factory.object.status = command.kwargs
            self.factory.object.operate()

class Watchdog:
    def __init__(self):
        self.status = {}
        self.prev_status = None

    def operate(self):
        if self.status and self.prev_status and self.prev_status.has_key('is_night') and self.prev_status.has_key('scheduler_zsun') and self.status.has_key('is_night') and self.status.has_key('scheduler_zsun') and self.status.has_key('dome') and self.status.has_key('dome_state'):
            if self.status['is_night'] == '0' and self.prev_status['is_night'] == '1':
                # Night has just ended, let's start postprocessing
                print "Night has just ended"
                # os.system('./fweb.py terminatealltasks')
                os.system('./fweb.py processall')

            if self.status['is_night'] == '1' and self.prev_status['is_night'] == '0':
                # Sunset, let's check whether system is working
                if self.status['is_weather_good'] == '1' and self.status['is_dome_open'] == '0':
                    print "Weather is good at sunset but dome is closed"
                    subject = "watchdog: weather is good at sunset but dome is closed"
                    send_email("Weather is good at sunset but dome is closed", subject=subject)

            if float(self.status['scheduler_zsun']) < 95.0 and float(self.prev_status['scheduler_zsun']) >= 95.0:
                # Ensure the mounts are stopped and cameras are turned off
                print "Sun is at 95 degrees, stopping the mounts and cameras"
                self.factory.message('broadcast_mounts stop')
                self.factory.message('broadcast_channels hw set_camera 0')
                self.factory.message('broadcast_channels hw set_cover 0')

                if self.status['dome'] == '1' and self.status['dome_state'] != '0':
                    print "Dome is not closed at sunrise"
                    self.factory.message('command_dome close_dome')
                    subject = "watchdog: dome is not closed at sunrise"
                    send_email("The dome is not closed at sunrise", subject=subject)

                # Check whether the chillers are on
                is_chillers_on = False
                for i in xrange(1, 1+int(self.status['nchannels'])):
                    if self.status['can_chiller%d_state' % i] == '0':
                        is_chillers_on = True

                if is_chillers_on:
                    print "Chillers are on on sunrise"
                    self.factory.message('command_can set_chillers power=0')

        self.prev_status = self.status

        if self.status['dome'] == '1':
            if self.status['dome_state'] != '0' and self.status['dome_state'] != '4' and self.status['dome_dehum_dome'] == '1':
                print "Dome is not closed and dehum dome is on"
                self.factory.message('command_dome stop_dehum_dome')

            if self.status['dome_state'] == '0' and self.status['dome_dehum_dome'] == '0':
                print "Dome is closed and dehum dome is off"
                self.factory.message('command_dome start_dehum_dome')

            if self.status['dome_dehum_channels'] == '0':
                print "Dehum channels is off"
                self.factory.message('command_dome start_dehum_channels')

        if self.status['dome'] == '1' and self.status['can'] == '1':
            if (self.status['can_chiller1_state'] == '1' or self.status['can_chiller2_state'] == '1' or self.status['can_chiller3_state'] == '1') and self.status['dome_vent_engineering'] == '1':
                print "Chillers are off and vent is on"
                self.factory.message('command_dome stop_vent')
            if (self.status['can_chiller1_state'] == '0' or self.status['can_chiller2_state'] == '0' or self.status['can_chiller2_state'] == '0') and self.status['dome_vent_engineering'] == '0':
                print "Chillers are on and vent is off"
                self.factory.message('command_dome start_vent')

        is_camera_on = False
        is_chillers_off = False
        for i in xrange(1, 1+int(self.status['nchannels'])):
            if not self.status.has_key('channel%d_state' % i):
                # No status for given camera yet
                continue

            if self.status['channel%d_state' % i] == '100' and self.status['channel%d_grabber' % i] == '1' and self.status['channel%d_hw_cover' % i] in ['0', '1'] and self.status['channel%d_hw_filter' % i] != 'Custom' and self.status['state'] not in ['manual', 'manual-init', 'activating', 'deactivating', 'standby']:
                # Error state of a channel, and the grabber is working
                self.factory.message('command_channel id=%d stop' % i)

            if self.status['channel%d_hw_camera' % i] == '1':
                is_camera_on = True

            if self.status['can_chiller%d_state' % i] == '1':
                is_chillers_off = True

        if is_camera_on and is_chillers_off:
            print "Cameras are on and chillers are off"
            self.factory.message('command_can set_chillers power=1')

# Main
if __name__ == '__main__':
    watchdog = Watchdog()

    watchdog.factory = Favor2ClientFactory(BeholderProtocol, watchdog)

    from twisted.internet import reactor
    reactor.connectTCP('localhost', 5560, watchdog.factory)

    reactor.run()
