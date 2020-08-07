#!/usr/bin/env python

from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtNetwork import *
from PyQt4 import uic
import os
import types

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

class Client(QMainWindow):
    def __init__(self):
        super(Client, self).__init__()

        ## UI
        self.ui = uic.loadUi(os.path.dirname(os.path.realpath(__file__)) + "/client.ui", self)

        ## Socket connection to daemon
        self.socket = QTcpSocket()
        self.host = "localhost"
        self.port = 5555
        self.connect(self.socket, SIGNAL("connected()"), self.slotConnected)
        self.connect(self.socket, SIGNAL("disconnected()"), self.slotDisconnected)
        self.connect(self.socket, SIGNAL("readyRead()"), self.slotReadyRead)
        self.connect(self.socket, SIGNAL("error(QAbstractSocket::SocketError)"), self.slotSocketError)
        self.socketBuffer = ""

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

    def setHost(self, host = 'localhost', port = 5555):
        self.host = host
        self.port = port
        self.ui.setWindowTitle("Client @ " + host + ":" + str(port))
        self.slotDisconnected()

    def slotConnect(self):
        self.socket.connectToHost(self.host, self.port)

    def slotConnected(self):
        self.message("Connected")

    def slotReconnect(self):
        if self.isConnected():
            self.socket.disconnectFromHost()
        else:
            self.slotDisconnected()

    def slotDisconnected(self):
        self.message("Disconnected")
        self.socketBuffer = ""
        self.slotConnect()

    def slotSocketError(self, error):
        if error == QTcpSocket.RemoteHostClosedError or error == QTcpSocket.ConnectionRefusedError:
            ## FIXME: Potentially we may start several such timers concurrently
            QTimer.singleShot(1000, self.slotConnect)

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

    def processCommand(self, string):
        #print "Command:", string

        self.ui.logTextEdit.append(string)

if __name__ == '__main__':
    import sys
    import signal
    import argparse

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    app = QApplication(sys.argv)

    args = immp.parse(' '.join(sys.argv))

    print 'Connecting to ' + args.kwargs.get('host', 'localhost') + ':' + args.kwargs.get('port', '5555')

    client = Client()
    client.show()

    client.setHost(args.kwargs.get('host', 'localhost'), int(args.kwargs.get('port', '5555')))
    client.setAttribute(Qt.WA_DeleteOnClose)

    client.connect(client, SIGNAL("closed()"), app.quit)

    if sys.platform == 'darwin':
        client.raise_()

    sys.exit(app.exec_())
