#!/bin/bash

rm -f out.log

function sendcmd {
echo $1
d=${2:-10}
sleep $d
}

while true; do
(
sendcmd "set_cover 1"

for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0; do sendcmd "set_filters "$i; done

sendcmd "reset_focus 1"
sendcmd "move_focus 100 1"
sendcmd "move_focus 10 1"
sendcmd "reset_focus 1"

sendcmd "set_mirror 0 0"
sendcmd "set_mirror -127 0"
sendcmd "set_mirror 0 0"
sendcmd "set_mirror 127 0"
sendcmd "set_mirror 0 0"
sendcmd "set_mirror 0 -127"
sendcmd "set_mirror 0 0"
sendcmd "set_mirror 0 127"

sendcmd "set_mirror 0 0"
sendcmd "set_mirror -127 -127"
sendcmd "set_mirror 0 0"
sendcmd "set_mirror 127 127"
sendcmd "set_mirror 0 0"

sendcmd "set_cover 0"
)|socat - TCP4:localhost:5556 >> out.log
echo >> out.log
done
