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
    explicit MainWindow(QWidget *parent = 0, char *hw_host = "localhost", int hw_port = 5556);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QTcpSocket *socketHW;
    QTimer *timerHW;
    QStatusBar *status;

    void sendHWCommand(const char *, int );

    char *hw_host;
    int hw_port;

private slots:
    void slotHWConnect();
    void slotHWReadyRead();
    void slotHWConnected();
    void slotHWDisconnected();
    void slotHWError(QAbstractSocket::SocketError);

    void slotTimerHWEvent();

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

    void slotHWSendCommand();
};

#endif // MAINWINDOW_H
