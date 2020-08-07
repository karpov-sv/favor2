#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtNetwork>
#include <QTimer>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0, char *hw_host = "localhost", int hw_port = 5556,
                        char *channel_host = "localhost", int channel_port = 5555,
                        char *grabber_host="localhost", int grabber_port = 10535);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QTcpSocket *socketHW;
    QTcpSocket *socketGrabber;
    QTcpSocket *socketChannel;
    QTimer *timerHW;
    QTimer *timerChannel;
    QStatusBar *status;

    void sendHWCommand(const char *, int );
    void sendGrabberCommand(const char *);
    void sendChannelCommand(const char *);

    char *hw_host;
    int hw_port;
    char *grabber_host;
    int grabber_port;
    char *channel_host;
    int channel_port;

private slots:
    void slotHWConnect();
    void slotHWReadyRead();
    void slotHWConnected();
    void slotHWDisconnected();
    void slotHWError(QAbstractSocket::SocketError);

    void slotGrabberConnect();
    void slotGrabberReadyRead();
    void slotGrabberConnected();
    void slotGrabberDisconnected();
    void slotGrabberError(QAbstractSocket::SocketError);

    void slotChannelConnect();
    void slotChannelReadyRead();
    void slotChannelConnected();
    void slotChannelDisconnected();
    void slotChannelError(QAbstractSocket::SocketError);

    void slotTimerHWEvent();
    void slotTimerChannelEvent();

    void slotFocusMin();
    void slotFocusMinus100();
    void slotFocusMinus10();
    void slotFocusPlus100();
    void slotFocusPlus10();
    void slotFocusMax();
    void slotFocus2Min();
    void slotFocus2Minus100();
    void slotFocus2Minus10();
    void slotFocus2Plus100();
    void slotFocus2Plus10();
    void slotFocus2Max();

    void slotIIOn();
    void slotIIOff();

    void slotCoverOpen();
    void slotCoverClose();

    void slotCelostateMove();

    void slotFiltersSet();

    void slotGrabberStart();
    void slotGrabberStop();

    void slotChannelSendCommand();
    void slotChannelDarks();
    void slotChannelAutofocus();
    void slotChannelQuit();

    void slotChannelProcessingStart();
    void slotChannelProcessingStop();
    void slotChannelStorageStart();
    void slotChannelStorageStop();
    void slotChannelGrabberStart();
    void slotChannelGrabberStop();
    void slotChannelIIStart();
    void slotChannelIIStop();
};

#endif // MAINWINDOW_H
