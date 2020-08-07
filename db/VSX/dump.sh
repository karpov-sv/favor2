#!/bin/sh

echo "DROP TABLE IF EXISTS vsx;" | psql favor2

echo "CREATE TABLE vsx (ra FLOAT, dec FLOAT, name TEXT, type TEXT, min FLOAT, max FLOAT, period FLOAT);" | psql favor2

bzcat vsx.txt.bz2 | awk '{FS=";"; gsub(/ /,"",$5); gsub(/ /,"",$7); gsub(/ /,"",$9); gsub(/ /,"",$14); gsub(/ /,"",$20); if($1*1 > 0 && $22*1 > 0) print $1","$2","$5","$7","$9","$14","$20}' | psql favor2 -c "COPY vsx (ra,dec,name,type,min,max,period) FROM stdin WITH NULL AS '' CSV ;"

echo "CREATE INDEX ON vsx (q3c_ang2ipix(ra,dec));" | psql favor2
echo "CREATE INDEX ON vsx (name);" | psql favor2
echo "CREATE INDEX ON vsx (type);" | psql favor2
echo "CREATE INDEX ON vsx (period);" | psql favor2


# alter table pickles add column var int default 0;
# update pickles p set var = 1 from vsx v where q3c_join(v.ra, v.dec, p.ra, p.dec, 0.01);
