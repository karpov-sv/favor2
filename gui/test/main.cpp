#include <QApplication>
#include "mainwindow.h"

extern "C" {
#include "utils.h"
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    char *hw_host = "localhost";
    int hw_port = 5556;
    char *channel_host = "localhost";
    int channel_port = 5555;
    char *grabber_host="localhost";
    int grabber_port = 10535;

    parse_args(argc, argv,
               "hw_host=%s", &hw_host,
               "hw_port=%d", &hw_port,
               "channel_host=%s", &channel_host,
               "channel_port=%d", &channel_port,
               "grabber_host=%s", &grabber_host,
               "grabber_port=%d", &grabber_port,
               NULL);

    MainWindow w(0, hw_host, hw_port, channel_host, channel_port, grabber_host, grabber_port);

    w.show();

    return a.exec();
}
