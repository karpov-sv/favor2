#!/bin/sh

SHAREDIR=`pg_config --sharedir`

dropdb favor2
createdb favor2
createlang plpgsql favor2

if [ -e $SHAREDIR/contrib/q3c.sql ]; then
	psql favor2 < $SHAREDIR/contrib/q3c.sql
else
	echo "No Q3C found"
        exit 1
fi

psql favor2 < auxiliary.sql
psql favor2 < favor2.sql
psql favor2 < scheduler.sql
psql favor2 < realtime.sql
psql favor2 < secondscale.sql
