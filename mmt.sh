#!/bin/bash

export SCREENDIR=~/.screen-favor2

if [ -z "`screen -ls|grep mmt`" ]; then
    screen -d -m -S mmt -c mmt.screenrc
    echo "Run in background, type the command again to connect"
else
    screen -x mmt -c mmt.screenrc -p mmt
fi

if [ -z "`screen -ls|grep mmt`" ]; then
    echo "Killing remaining processes"
    killall channel hw fake_hw fake_grabber fake_mount webchannel beholder fake_dome fake_weather scheduler_server logger
    screen -wipe mmt
fi

unset SCREENDIR
