#!/bin/bash

export SCREENDIR=~/.screen-favor2

if [ -z "`screen -ls|grep tortora`" ]; then
    screen -d -m -S tortora -c tortora.screenrc
    echo "Run in background, type the command again to connect"
else
    screen -x tortora -c tortora.screenrc -p channel
fi

if [ -z "`screen -ls|grep tortora`" ]; then
    echo "Killing remaining processes"
    killall t_hw t_rem t_beholder logger channel webchannel
    screen -wipe tortora
fi

unset SCREENDIR
