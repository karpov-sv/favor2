#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTimer>

extern "C"{
#include "utils.h"
#include "command.h"
};

MainWindow::MainWindow(QWidget *parent,
                       char *hw_host_in, int hw_port_in) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QString tmp;

    hw_host = hw_host_in;
    hw_port = hw_port_in;

    ui->setupUi(this);
    status = statusBar();

    setWindowTitle(tmp.sprintf("HW Controller @ %s:%d", hw_host, hw_port));

    socketHW = new QTcpSocket(this);
    connect(socketHW, SIGNAL(connected()), SLOT(slotHWConnected()));
    connect(socketHW, SIGNAL(disconnected()), SLOT(slotHWDisconnected()));
    connect(socketHW, SIGNAL(readyRead()), SLOT(slotHWReadyRead()));
    connect(socketHW, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(slotHWError(QAbstractSocket::SocketError)));

    slotHWConnect();

    timerHW = new QTimer(this);
    timerHW->setSingleShot(true);
    connect(timerHW, SIGNAL(timeout()), this, SLOT(slotTimerHWEvent()));

    connect(ui->pushButton_min, SIGNAL(clicked()), this, SLOT(slotFocusMin()));
    connect(ui->pushButton_m100, SIGNAL(clicked()), this, SLOT(slotFocusMinus100()));
    connect(ui->pushButton_m10, SIGNAL(clicked()), this, SLOT(slotFocusMinus10()));
    connect(ui->pushButton_p10, SIGNAL(clicked()), this, SLOT(slotFocusPlus10()));
    connect(ui->pushButton_p100, SIGNAL(clicked()), this, SLOT(slotFocusPlus100()));
    connect(ui->pushButton_max, SIGNAL(clicked()), this, SLOT(slotFocusMax()));
    connect(ui->pushButton_2min, SIGNAL(clicked()), this, SLOT(slotFocus2Min()));
    connect(ui->pushButton_2m100, SIGNAL(clicked()), this, SLOT(slotFocus2Minus100()));
    connect(ui->pushButton_2m10, SIGNAL(clicked()), this, SLOT(slotFocus2Minus10()));
    connect(ui->pushButton_2p10, SIGNAL(clicked()), this, SLOT(slotFocus2Plus10()));
    connect(ui->pushButton_2p100, SIGNAL(clicked()), this, SLOT(slotFocus2Plus100()));
    connect(ui->pushButton_2max, SIGNAL(clicked()), this, SLOT(slotFocus2Max()));

    connect(ui->pushButtonIIOn, SIGNAL(clicked()), this, SLOT(slotIIOn()));
    connect(ui->pushButtonIIOff, SIGNAL(clicked()), this, SLOT(slotIIOff()));

    connect(ui->pushButtonCoverOpen, SIGNAL(clicked()), this, SLOT(slotCoverOpen()));
    connect(ui->pushButtonCoverClose, SIGNAL(clicked()), this, SLOT(slotCoverClose()));

    connect(ui->sliderCelostate0, SIGNAL(sliderReleased()), this, SLOT(slotCelostateMove()));
    connect(ui->sliderCelostate1, SIGNAL(sliderReleased()), this, SLOT(slotCelostateMove()));

    connect(ui->pushButtonFiltersSet, SIGNAL(clicked()), this, SLOT(slotFiltersSet()));

    connect(ui->actionFocusMin, SIGNAL(triggered()), this, SLOT(slotFocusMin()));
    connect(ui->actionFocusMax, SIGNAL(triggered()), this, SLOT(slotFocusMax()));

    connect(ui->hwLineEdit, SIGNAL(returnPressed()), this, SLOT(slotHWSendCommand()));
}

MainWindow::~MainWindow()
{
    delete ui;

    delete socketHW;
    delete timerHW;
}

void MainWindow::slotHWConnect()
{
    socketHW->connectToHost(hw_host, hw_port);
}

void MainWindow::slotHWConnected()
{
    ui->labelHWConnected->setText("<font color='green'>Connected</font>");
    timerHW->start(100);
}

void MainWindow::slotHWDisconnected()
{
    ui->labelHWConnected->setText("<font color='red'>Disconnected</font>");
    timerHW->stop();
    slotHWConnect();
}

void MainWindow::slotHWError(QAbstractSocket::SocketError err)
{
    if(err == QTcpSocket::RemoteHostClosedError ||
       err == QTcpSocket::ConnectionRefusedError)
        QTimer::singleShot(1000, this, SLOT(slotHWConnect()));
    timerHW->stop();
}

void MainWindow::slotHWReadyRead()
{
    char buffer[1024];
    int length;

    do {
        length = socketHW->readLine(buffer, sizeof(buffer));
        if(length){
            command_str *command = command_parse(buffer);

            if(command_match(command, "hw_status")){
                QString tmp;
                int connected = -1;
                int focus = 0;
                int focus2 = 0;
                int ii_power = 0;
                int cover = 0;
                double cam_t = -1;
                double cam_h = -1;
                double cam_d = -1;
                double cel_t = -1;
                double cel_h = -1;
                double cel_d = -1;
                int pos0 = -1;
                int pos1 = -1;
                int filters[4] = {-1, -1, -1, -1};

                command_args(command,
                             "connected=%d", &connected,
                             "focus=%d", &focus,
                             "focus2=%d", &focus2,
                             "ii_power=%d", &ii_power,
                             "cover=%d", &cover,
                             "camera_temperature=%lf", &cam_t,
                             "camera_humidity=%lf", &cam_h,
                             "camera_dewpoint=%lf", &cam_d,
                             "celostate_temperature=%lf", &cel_t,
                             "celostate_humidity=%lf", &cel_h,
                             "celostate_dewpoint=%lf", &cel_d,
                             "celostate_pos0=%d", &pos0,
                             "celostate_pos1=%d", &pos1,
                             "filter0=%d", &filters[0],
                             "filter1=%d", &filters[1],
                             "filter2=%d", &filters[2],
                             "filter3=%d", &filters[3],
                             NULL);

                if(connected){
                    ui->labelHWFocus->setText(tmp.sprintf("%d", focus));
                    ui->labelHWFocus2->setText(tmp.sprintf("%d", focus2));
                    ui->labelII->setText(ii_power ? "<font color='green'>On</font>" : "<font color='red'>Off</font>");
                    if(cover == 1)
                        ui->labelCover->setText("<font color='green'>Open</font>");
                    else if(cover == 0)
                        ui->labelCover->setText("<font color='red'>Closed</font>");
                    else
                        ui->labelCover->setText("unknown");
                    ui->labelCCConditions->setText(tmp.sprintf("Camera: T=%g H=%g D=%g, Celostate T=%g H=%g D=%g", cam_t, cam_h, cam_d, cel_t, cel_h, cel_d));
                    ui->labelCelostate->setText(tmp.sprintf("%d : %d", pos0, pos1));

                    tmp = "";
                    for(int i = 0; i < 4; i++){
                        tmp.append(filters[i] == 1 ? "<font color='green'> 1 </font>" :
                                   (filters[i] == 0 ? "<font color='gray'> 0 </font>" :
                                    (filters[i] == -1 ? "<font color='red'> ? </font>" : "<font color='red'> ! </font>")));
                    }
                    ui->labelFilters->setText(tmp);
                } else {
                    ui->labelHWFocus->setText("unknown");
                    ui->labelHWFocus2->setText("unknown");
                    ui->labelII->setText("unknown");
                    ui->labelCover->setText("unknown");
                    ui->labelCCConditions->setText("unknown");
                    ui->labelCelostate->setText("unknown");
                    ui->labelFilters->setText("unknown");
                }
            } else
                ui->hwLog->append(buffer);

            command_delete(command);
        }
    } while(length);
}

void MainWindow::sendHWCommand(const char *command, int tmp=0)
{
    if(socketHW->state() == QAbstractSocket::ConnectedState){
        socketHW->write(command, strlen(command) + 1);
    }
}

void MainWindow::slotTimerHWEvent()
{
    sendHWCommand("get_hw_status");

    timerHW->start(100);
}

void MainWindow::slotFocusMin()
{
    sendHWCommand("reset_focus", 7);
}

void MainWindow::slotFocusMinus100()
{
    sendHWCommand("move_focus shift=-100");
}

void MainWindow::slotFocusMinus10()
{
    sendHWCommand("move_focus shift=-10");
}

void MainWindow::slotFocusPlus100()
{
    sendHWCommand("move_focus shift=100");
}

void MainWindow::slotFocusPlus10()
{
    sendHWCommand("move_focus shift=10");
}
void MainWindow::slotFocusMax()
{
    sendHWCommand("move_focus shift=4095");
}

void MainWindow::slotFocus2Min()
{
    sendHWCommand("reset_focus id=1", 7);
}

void MainWindow::slotFocus2Minus100()
{
    sendHWCommand("move_focus shift=-100 id=1");
}

void MainWindow::slotFocus2Minus10()
{
    sendHWCommand("move_focus shift=-10 id=1");
}

void MainWindow::slotFocus2Plus100()
{
    sendHWCommand("move_focus shift=100 id=1");
}

void MainWindow::slotFocus2Plus10()
{
    sendHWCommand("move_focus shift=10 id=1");
}

void MainWindow::slotFocus2Max()
{
    sendHWCommand("move_focus shift=4095 id=1");
}

void MainWindow::slotIIOn()
{
    sendHWCommand("set_ii_power 1");
}

void MainWindow::slotIIOff()
{
    sendHWCommand("set_ii_power 0");
}

void MainWindow::slotCoverOpen()
{
    sendHWCommand("set_cover 1");
}

void MainWindow::slotCoverClose()
{
    sendHWCommand("set_cover 0");
}

void MainWindow::slotCelostateMove()
{
    QString tmp;
    int pos0 = ui->sliderCelostate0->value();
    int pos1 = ui->sliderCelostate1->value();

    sendHWCommand(tmp.sprintf("set_mirror %d %d", pos0, pos1).toAscii());
}

void MainWindow::slotFiltersSet()
{
    QString tmp;
    int value = 0;

    if(ui->checkBoxFilter0->isChecked())
        value += 1;
    if(ui->checkBoxFilter1->isChecked())
        value += 2;
    if(ui->checkBoxFilter2->isChecked())
        value += 4;
    if(ui->checkBoxFilter3->isChecked())
        value += 8;

    sendHWCommand(tmp.sprintf("set_filters %d", value).toAscii());
}

void MainWindow::slotHWSendCommand()
{
    sendHWCommand(ui->hwLineEdit->text().toAscii());
}
