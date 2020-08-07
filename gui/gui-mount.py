#!/usr/bin/env python

from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtNetwork import *
from PyQt4 import uic
import os
import types

import urllib, urllib2
import xml.dom.minidom as minidom
import re

from frame import Frame

import immp

class MountGUI(QMainWindow):
    def __init__(self):
        super(MountGUI, self).__init__()

        ## UI
        self.ui = uic.loadUi(os.path.dirname(os.path.realpath(__file__)) + "/gui-mount.ui", self)

        ## Timer for periodic request of daemon state
        self.timer = QTimer()
        self.timer.setSingleShot(True)
        self.connect(self.timer, SIGNAL("timeout()"), self.slotTimerEvent)

        ## Socket connection to daemon
        self.socket = QTcpSocket()
        self.host = "localhost"
        self.port = 5562
        self.connect(self.socket, SIGNAL("connected()"), self.slotConnected)
        self.connect(self.socket, SIGNAL("disconnected()"), self.slotDisconnected)
        self.connect(self.socket, SIGNAL("readyRead()"), self.slotReadyRead)
        self.connect(self.socket, SIGNAL("error(QAbstractSocket::SocketError)"), self.slotSocketError)
        self.socketBuffer = ""

        # Buttons
        self.connect(self.ui.trackingStartPushButton, SIGNAL("clicked()"), lambda : self.sendCommand("tracking_start"))
        self.connect(self.ui.trackingStopPushButton, SIGNAL("clicked()"), lambda : self.sendCommand("tracking_stop"))
        self.connect(self.ui.stopPushButton, SIGNAL("clicked()"), lambda : self.sendCommand("stop"))
        self.connect(self.ui.repointPushButton, SIGNAL("clicked()"), self.slotRepointPressed)
        self.connect(self.ui.targetLineEdit, SIGNAL("returnPressed()"), self.slotRepointPressed)

        ## Command line
        self.connect(self.ui.commandLineEdit, SIGNAL("returnPressed()"), self.commandLineReturn)

        ## Initial focus to command line
        self.ui.commandLineEdit.setFocus()

        self.ui.commandLineEdit.history = []
        self.ui.commandLineEdit.historyPos = 0
        self.ui.commandLineEdit.keyPressEvent = types.MethodType(lineKeyPressEvent, self.ui.commandLineEdit)

    def message(self, string=""):
        self.ui.statusbar.showMessage(string)

    def closeEvent(self, event):
        self.emit(SIGNAL("closed()"))
        QMainWindow.closeEvent(self, event)

    def isConnected(self):
        return self.socket.state() == QAbstractSocket.ConnectedState

    def commandLineReturn(self):
        self.sendCommand(self.ui.commandLineEdit.text())
        self.ui.logTextEdit.append("<font color=blue>> " + self.ui.commandLineEdit.text() + "</font>")
        self.ui.commandLineEdit.setText("")

    def sendCommand(self, command):
        if self.isConnected():
            string = command.__str__()
            self.socket.write(string)
            self.socket.write('\0')

    def setHost(self, host = 'localhost', port = 5562):
        self.host = host
        self.port = port
        self.ui.setWindowTitle("Mount @ " + host + ":" + str(port))
        self.slotDisconnected()

    def slotConnect(self):
        self.socket.connectToHost(self.host, self.port)

    def slotConnected(self):
        self.message("Connected")
        self.timer.start(100)

    def slotReconnect(self):
        if self.isConnected():
            self.socket.disconnectFromHost()
        else:
            self.slotDisconnected()

    def slotDisconnected(self):
        self.message("Disconnected")
        self.timer.stop()
        self.socketBuffer = ""
        self.slotConnect()

    def slotSocketError(self, error):
        if error == QTcpSocket.RemoteHostClosedError or error == QTcpSocket.ConnectionRefusedError:
            ## FIXME: Potentially we may start several such timers concurrently
            QTimer.singleShot(1000, self.slotConnect)
        self.timer.stop()

    def slotTimerEvent(self):
        self.sendCommand("get_mount_status")
        self.timer.start(100)

    def slotReadyRead(self):
        while self.socket.size():
            string = self.socketBuffer
            stringCompleted = False

            while self.socket.size():
                (result, ch) = self.socket.getChar()

                if result and ch and ch != '\0' and ch != '\n':
                    string += ch
                elif result:
                    stringCompleted = True
                    break
                else:
                    break

            ## TODO: store incomplete lines for surther processing
            if string and stringCompleted:
                self.processCommand(string)
                self.socketBuffer = ""
            else:
                self.socketBuffer = string

    def updateStatus(self, status):
        self.ui.stateLabel.setText({-1:'<font color="red">unknown</font>',
                                    0:'<font color="gray">IDLE</font>',
                                    1:'<font color="blue">SLEWING</font>',
                                    2:'<font color="green">TRACKING</font>'}.get(status, "none"))
        self.ui.trackingStartPushButton.setEnabled(status == 0)
        self.ui.trackingStopPushButton.setEnabled(status == 2)

    def processCommand(self, string):
        #print "Command:", string

        command = immp.parse(string.__str__())

        if command.name() == 'mount_status':
            self.statusLabel.setText((string.__str__())[13:])
            self.updateStatus(int(command.kwargs.get('status', '-1')))

            self.ui.raLabel.setText(degtostring(float(command.kwargs.get('ra', '0'))/15.0))
            self.ui.raLabel.setToolTip(command.kwargs.get('ra', 'unknown'))

            self.ui.decLabel.setText(degtostring(float(command.kwargs.get('dec', '0')), True))
            self.ui.decLabel.setToolTip(command.kwargs.get('dec', 'unknown'))

            self.ui.nextRALabel.setText(degtostring(float(command.kwargs.get('next_ra', '0'))/15.0))
            self.ui.nextRALabel.setToolTip(command.kwargs.get('next_ra', 'unknown'))

            self.ui.nextDecLabel.setText(degtostring(float(command.kwargs.get('next_dec', '0')), True))
            self.ui.nextDecLabel.setToolTip(command.kwargs.get('next_dec', 'unknown'))
        else:
            self.ui.logTextEdit.append(string)

    def slotRepointPressed(self):
        name, ra, dec = resolve(self.ui.targetLineEdit.text())
        if name:
            self.message("Resolved to %s: %g %g" % (name, ra, dec))
            self.sendCommand("repoint ra=%g dec=%g" % (ra, dec))
        else:
            self.message("Can't resolve target")

def lineKeyPressEvent(self, event):
    if event.key() == Qt.Key_Up:
        if self.historyPos < len(self.history):
            self.historyPos = self.historyPos + 1
            self.setText(self.history[-self.historyPos])
    elif event.key() == Qt.Key_Down:
        if self.historyPos > 0:
            self.historyPos = self.historyPos - 1
            if self.historyPos == 0:
                self.setText("")
            else:
                self.setText(self.history[-self.historyPos])
    else:
        if event.key() == Qt.Key_Return:
            if not len(self.history) or (self.text() and self.text() != self.history[-1]):
                self.history.append(self.text())
            self.historyPos = 0
        QLineEdit.keyPressEvent(self, event)

def degtostring(value, plus=False):
    sign = cmp(value, 0)
    value = abs(value)

    dd = int(value)
    mm = int((value - dd)*60)
    ss = int((value - dd)*3600 - mm*60)

    string = ""

    if sign == -1:
        string = "-"
    elif plus:
        string = "+"

    string += "%02d %02d %02d" % (dd, mm, ss)

    return string

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

def resolve(string = ''):
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
    import sys
    import signal
    import argparse

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    app = QApplication(sys.argv)

    args = immp.parse(' '.join(sys.argv))

    print 'Connecting to ' + args.kwargs.get('host', 'localhost') + ':' + args.kwargs.get('port', '5562')

    client = MountGUI()
    client.show()

    client.setHost(args.kwargs.get('host', 'localhost'), int(args.kwargs.get('port', '5562')))
    client.setAttribute(Qt.WA_DeleteOnClose)

    client.connect(client, SIGNAL("closed()"), app.quit)

    if sys.platform == 'darwin':
        client.raise_()

    sys.exit(app.exec_())
