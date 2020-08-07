#!/bin/bash

(
echo "REM_NIGHT_BEGIN"
sleep 3
echo "REM_DOME_OPENED"
sleep 3

echo "REM_WILL_REPOINT_TO 1 2 70 test"
sleep 3

echo "REM_TRACKING_STARTED"
sleep 5
echo "REM_TRACKING_STOPPED"
sleep 5

echo "REM_DOME_CLOSED"
sleep 3
echo "REM_NIGHT_END"
sleep 3

)|socat STDIO TCP4:localhost:5566
