#!/usr/bin/env python

from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtNetwork import *
from PyQt4 import uic
import os
import types
import tempfile

from frame import Frame

import immp

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

class ChannelGUI(QMainWindow):
    mount_host = 'localhost'
    mount_port = 5562
    mount_command = './mount.py'

    def __init__(self):
        super(ChannelGUI, self).__init__()

        ## UI
        self.ui = uic.loadUi(os.path.dirname(os.path.realpath(__file__)) + "/gui-channel.ui", self)

        ## Timer for periodic request of daemon state
        self.timer = QTimer()
        self.timer.setSingleShot(True)
        self.connect(self.timer, SIGNAL("timeout()"), self.slotTimerEvent)

        ## Timer for periodic request of current image
        self.timerImage = QTimer()
        self.timerImage.setSingleShot(True);
        self.connect(self.timerImage, SIGNAL("timeout()"), self.slotTimerImageEvent)

        ## Socket connection to daemon
        self.socket = QTcpSocket()
        self.host = "localhost"
        self.port = 5555
        self.connect(self.socket, SIGNAL("connected()"), self.slotConnected)
        self.connect(self.socket, SIGNAL("disconnected()"), self.slotDisconnected)
        self.connect(self.socket, SIGNAL("readyRead()"), self.slotReadyRead)
        self.connect(self.socket, SIGNAL("error(QAbstractSocket::SocketError)"), self.slotSocketError)
        self.binaryMode = False
        self.binaryLength = 0
        self.socketBuffer = ""

        ## Current image view
        self.image = Frame()
        self.image.setWindowTitle("Current Frame")

        # Check Box
        self.connect(self.ui.imageCheckBox, SIGNAL("stateChanged(int)"), self.toggleImage)
        self.connect(self.image, SIGNAL("closed()"),
                     lambda : self.ui.imageCheckBox.setCheckState(False))

        ## Command line
        self.connect(self.ui.commandLineEdit, SIGNAL("returnPressed()"), self.commandLineReturn)

        ## Menubar
        self.connect(self.ui.actionMountFix, SIGNAL("activated()"), self.mountFix)

        ## Initial focus to command line
        self.ui.commandLineEdit.setFocus()

        self.ui.commandLineEdit.history = []
        self.ui.commandLineEdit.historyPos = 0
        self.ui.commandLineEdit.keyPressEvent = types.MethodType(lineKeyPressEvent, self.ui.commandLineEdit)

    def toggleImage(self, state):
        if state:
            self.image.show()
            self.slotTimerImageEvent()
        else:
            self.image.hide()
            self.timerImage.stop()

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

    def setHost(self, host = 'localhost', port = 5555):
        self.host = host
        self.port = port
        self.ui.setWindowTitle("Channel @ " + host + ":" + str(port))
        self.slotDisconnected()

    def slotConnect(self):
        self.socket.connectToHost(self.host, self.port)

    def message(self, string=""):
        self.ui.statusbar.showMessage(string)

    def slotConnected(self):
        self.message("Connected")
        self.timer.start(100)
        self.timerImage.start(1000)

    def slotReconnect(self):
        if self.isConnected():
            self.socket.disconnectFromHost()
        else:
            self.slotDisconnected()

    def slotDisconnected(self):
        self.message("Disconnected")
        self.timer.stop()
        self.timerImage.stop()
        self.socketBuffer = ""
        self.slotConnect()

    def slotSocketError(self, error):
        if error == QTcpSocket.RemoteHostClosedError or error == QTcpSocket.ConnectionRefusedError:
            ## FIXME: Potentially we may start several such timers concurrently
            QTimer.singleShot(1000, self.slotConnect)
        self.timer.stop()
        self.timerImage.stop()

    def slotTimerEvent(self):
        self.sendCommand("get_channel_status")
        self.timer.start(100)

    def slotTimerImageEvent(self):
        if self.isConnected() and self.ui.imageCheckBox.isChecked():
            self.sendCommand("get_current_frame scale=" + str(self.ui.scaleSpinBox.value()))
        else:
            self.timerImage.start(1000)

    def slotReadyRead(self):
        while self.socket.size():
            if self.binaryMode:
                if self.socket.size() >= self.binaryLength:
                    buffer = self.socket.read(self.binaryLength)
                    self.binaryMode = False
                    self.binaryLength = 0

                    i = QImage()
                    i.loadFromData(buffer)
                    if not i.isNull():
                        self.image.setImage(i)

                    ## FIXME: just in case
                    self.timerImage.start(1000)
                else:
                    break
            else:
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

    def processCommand(self, string):
        #print "Command:", string

        command = immp.parse(string.__str__())

        if command.name() == 'channel_status':
            self.statusLabel.setText((string.__str__())[15:])
        elif command.name() == 'current_frame':
            self.binaryMode = True
            self.binaryLength = int(command.kwargs.get('length', 0))
            self.image.message('mean = ' + command.kwargs.get('mean', 'unknown'))
        elif command.name() == 'get_current_frame_timeout' or command.name() == 'get_current_frame_error':
            self.slotTimerImageEvent()
        elif command.name() == 'get_current_frame_done':
            pass
        else:
            self.ui.logTextEdit.append(string)

    def mountFix(self):
        if self.image.image:
            self.message("Perfrming the astrometry and sending coordinates to the mount")

            filename = tempfile.mktemp(suffix=".jpg")
            self.image.saveImage(filename)

            os.system("%s --host %s --port %d -f %s" % (self.mount_command, self.mount_host, self.mount_port, filename))

            os.unlink(filename)

if __name__ == '__main__':
    import sys
    import signal
    import argparse

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    app = QApplication(sys.argv)

    args = immp.parse(' '.join(sys.argv))

    print 'Connecting to ' + args.kwargs.get('host', 'localhost') + ':' + args.kwargs.get('port', '5555')

    client = ChannelGUI()
    client.show()

    client.setHost(args.kwargs.get('host', 'localhost'), int(args.kwargs.get('port', '5555')))
    client.setAttribute(Qt.WA_DeleteOnClose)

    client.mount_host = args.kwargs.get('mount_host', 'localhost')
    client.mount_port = args.kwargs.get('mount_port', 5562)
    client.mount_command = args.kwargs.get('mount_command', './mount.py')

    client.connect(client, SIGNAL("closed()"), app.quit)

    if sys.platform == 'darwin':
        client.raise_()

    sys.exit(app.exec_())
