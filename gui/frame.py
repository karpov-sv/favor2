#!/usr/bin/env python

from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4 import uic
import types
import os

# Methods
def sceneMouseMoveEvent(self, ev):
    self.emit(SIGNAL("mouseMoved"), ev.scenePos())

def viewMousePressEvent(self, ev):
    if not self.rubber and ev.button() == Qt.LeftButton:
        self.origin = ev.pos()
        self.rubber = QRubberBand(QRubberBand.Rectangle, self)
        self.rubber.setGeometry(QRect(self.origin, QSize()))
        self.rubber.show()
    else:
        QGraphicsView.mousePressEvent(self, ev)

def viewMouseMoveEvent(self, ev):
    if self.rubber:
        self.rubber.setGeometry(QRect(self.origin, ev.pos()).normalized())
        self.emit(SIGNAL("regionChanged"), self.mapToScene(self.origin), self.mapToScene(ev.pos()))
    else:
        QGraphicsView.mouseMoveEvent(self, ev)

def viewMouseReleaseEvent(self, ev):
    if self.rubber:
        self.rubber.hide()
        self.rubber = None

        if self.origin != ev.pos():
            self.emit(SIGNAL("regionDefined"), self.mapToScene(self.origin), self.mapToScene(ev.pos()))
    else:
        QGraphicsView.mouseReleaseEvent(self, ev)

# Main class
class Frame(QMainWindow):
    def __init__(self):
        super(Frame, self).__init__()

        self.ui = uic.loadUi(os.path.dirname(os.path.realpath(__file__)) + "/frame.ui", self)

        self.ui.status.showMessage("Hi")

        self.scene = QGraphicsScene()
        self.view.setScene(self.scene)

        self.pixmap = QGraphicsPixmapItem()
        self.scene.addItem(self.pixmap)
        self.image = None
        self.currentZoom = 1.0

        self.roi = None

        self.gamma = 0.5
        self.ui.slider.setValue(100*self.gamma)

        self.connect(self.ui.actionSave, SIGNAL("activated()"), self.slotSave)

        self.connect(self.ui.actionZoomIn, SIGNAL("activated()"), lambda : self.zoom(1.2))
        self.connect(self.ui.actionZoomOut, SIGNAL("activated()"), lambda : self.zoom(1.0/1.2))
        self.connect(self.ui.actionZoomReset, SIGNAL("activated()"), lambda : self.zoom(1.0))
        self.connect(self.ui.actionZoomReset, SIGNAL("activated()"), lambda : self.zoom(1.0))
        #self.connect(self.ui.actionClearMarks, SIGNAL("activated()"), self.clearMarks)
        self.connect(self.ui.actionClearMarks, SIGNAL("activated()"), self.slotRegionReset)

        self.ui.layout().setSizeConstraint(QLayout.SetFixedSize)

        self.scene.mouseMoveEvent = types.MethodType(sceneMouseMoveEvent, self.scene)
        self.view.setMouseTracking(True)

        # Rubberband selection
        self.view.rubber = None
        self.view.mousePressEvent = types.MethodType(viewMousePressEvent, self.view)
        self.view.mouseMoveEvent = types.MethodType(viewMouseMoveEvent, self.view)
        self.view.mouseReleaseEvent = types.MethodType(viewMouseReleaseEvent, self.view)

        # Signals
        self.connect(self.scene, SIGNAL("mouseMoved"), lambda p:
                     self.ui.status.showMessage(str(int(p.x())) + ", " + str(int(p.y()))))
        self.connect(self.view, SIGNAL("regionChanged"), lambda p1, p2:
                     self.ui.status.showMessage(str(int(p1.x())) + ", " + str(int(p1.y())) + " - "
                                                + str(int(p2.x())) + ", " + str(int(p2.y()))))

        #self.connect(self.view, SIGNAL("regionDefined"), lambda p1, p2: self.addMark(p1, p2))
        self.connect(self.view, SIGNAL("regionDefined"), self.slotRegionDefined)
        self.connect(self.ui.slider, SIGNAL("valueChanged(int)"), lambda x: self.setGamma(1.0*x/100))

    def closeEvent(self, event):
        self.emit(SIGNAL("closed()"))
        QWidget.closeEvent(self, event)

    def setGamma(self, value):
        if value < 0.0 or value > 1.0:
            value = 0.5

        self.gamma = value
        self.updateImage()

    def updateImage(self):
        if self.gamma > 0 and self.gamma < 1:
            a = (self.gamma - 0.5)/self.gamma/(self.gamma - 1)
        else:
            a = 0
        for i in range(256):
            x = 1.0*i/255;
            y = int(255.0*(a*x*x + (1.0 - a)*x))
            y = min(max(y, 0), 255)

            self.image.setColor(i, qRgb(y, y, y))

        self.pixmap.setPixmap(QPixmap.fromImage(self.image))

    def setImage(self, image):
        self.image = image.convertToFormat(QImage.Format_Indexed8)
        self.updateImage()
        self.scene.setSceneRect(0, 0, self.image.size().width(), self.image.size().height())
        self.ui.view.setFixedSize(self.ui.view.sizeHint())

    def zoom(self, value):
        if value == 1.0:
            self.ui.view.scale(1.0/self.currentZoom, 1.0/self.currentZoom);
            self.currentZoom = 1.0;
        else:
            self.ui.view.scale(value, value);
            self.currentZoom *= value

        self.ui.view.setFixedSize(self.ui.view.sizeHint())

    def addMark(self, p1, p2):
        rect = QGraphicsRectItem(QRectF(p1, p2))
        rect.setPen(QPen(QColor(255, 0, 0)))
        self.scene.addItem(rect)

    def clearMarks(self):
        for obj in self.scene.items():
            if type(obj) == QGraphicsRectItem:
                self.scene.removeItem(obj)

    def slotRegionReset(self):
        self.resetRoi()
        self.emit(SIGNAL("roiReset"))

    def resetRoi(self):
        if self.roi:
            self.scene.removeItem(self.roi)
            self.roi = None

    def slotRegionDefined(self, p1, p2):
        self.setRoi(p1.x(), p1.y(), p2.x(), p2.y())
        #self.gaussfitRoi(p1.x(), p1.y(), p2.x(), p2.y())
        self.emit(SIGNAL("roiChanged"), int(p1.x()), int(p1.y()), int(p2.x()), int(p2.y()))

    def setRoi(self, x1, y1, x2, y2):
        p1 = QPointF(x1, y1)
        p2 = QPointF(x2, y2)

        if self.roi:
            self.roi.setRect(QRectF(p1, p2))
            self.view.update()
        else:
            self.roi = QGraphicsRectItem(QRectF(p1, p2))
            self.roi.setPen(QPen(QColor(255, 0, 0)))
            self.scene.addItem(self.roi)

    def message(self, string):
        self.ui.status.showMessage(string)

    def saveImage(self, filename):
        if self.image and filename:
            self.image.save(filename)

    def slotSave(self):
        filename = QFileDialog.getSaveFileName(self, caption="Save Current Image", directory=os.getcwd(), filter="JPEG Images(*.jpg)")
        #filename = QFileDialog.getSaveFileName(self, caption="Save Current Image", directory=os.getcwd(), filter="JPEG Images(*.jpg)", options=QFileDialog.DontUseNativeDialog)

        if filename:
            self.saveImage(filename)

# Main
if __name__ == '__main__':
    import sys
    import signal

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    app = QApplication(sys.argv)

    frame = Frame()
    frame.show()

    frame.setImage(QImage("image2.png"));

    frame.connect(frame, SIGNAL("closed()"), app.quit)

    # frame.connect(frame, SIGNAL("roiChanged"), lambda x1, y1, x2, y2: print(x1, y1, x2, y2))
    # frame.connect(frame, SIGNAL("roiReset"), lambda : print("reset"))

    sys.exit(app.exec_())
