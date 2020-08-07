#!/bin/sh

for name in channel hw allsky beholder scheduler_server meteo dome; do
./test_license $name > $name.license
done
