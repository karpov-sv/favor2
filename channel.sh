#!/bin/bash

export SCREENDIR=~/.screen-favor2

if [ -z "`screen -ls|grep channel`" ]; then
    screen -d -m -S channel -c channel.screenrc
    echo "Run in background, type the command again to connect"
else
    screen -x channel -c channel.screenrc -p channel
fi

if [ -z "`screen -ls|grep channel`" ]; then
    echo "Killing remaining processes"
    killall hw channel webchannel.py logger mount
    screen -wipe channel
fi

unset SCREENDIR
