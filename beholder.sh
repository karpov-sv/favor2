#!/bin/bash

export SCREENDIR=~/.screen-favor2

if [ -z "`screen -ls|grep beholder`" ]; then
    screen -d -m -S beholder -c beholder.screenrc
    echo "Run in background, type the command again to connect"
else
    screen -x beholder -c beholder.screenrc -p beholder
fi

if [ -z "`screen -ls|grep beholder`" ]; then
    echo "Killing remaining processes"
    killall beholder fake_dome fake_weather scheduler_server logger
fi

unset SCREENDIR
