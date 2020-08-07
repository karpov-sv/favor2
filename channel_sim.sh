#!/bin/bash

export SCREENDIR=~/.screen-favor2

if [ -z "`screen -ls|grep channel`" ]; then
    screen -d -m -S channel_sim -c channel_sim.screenrc
    echo "Run in background, type the command again to connect"
else
    screen -x channel_sim -c channel_sim.screenrc -p channel
fi

if [ -z "`screen -ls|grep channel`" ]; then
    echo "Killing remaining processes"
    killall hw fake_hw fake_grabber fake_mount channel webchannel.py logger
    # Rough hack for OSX
    pkill -f ython.*webchannel.py
    screen -wipe channel_sim
fi

unset SCREENDIR
