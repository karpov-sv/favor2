#!/bin/sh

echo "DROP TABLE IF EXISTS wbvr;" | psql favor2

echo "CREATE TABLE wbvr (ra FLOAT, dec FLOAT, B FLOAT, V FLOAT, R FLOAT);" | psql favor2

bzcat SIRFUL.DAT.bz2 | awk '{FS="|"; if($22*1 > 0 && $23*1 != 0) print $22"," $23"," ($31+$33)"," $31"," ($31-$34)}' | psql favor2 -c "COPY wbvr (ra,dec,B,V,R) FROM stdin CSV;"

echo "CREATE INDEX wbvr_q3c_idx ON wbvr (q3c_ang2ipix(ra,dec));" | psql favor2
echo "CREATE INDEX wbvr_B_idx ON wbvr (B);" | psql favor2
echo "CREATE INDEX wbvr_V_idx ON wbvr (V);" | psql favor2
echo "CREATE INDEX wbvr_R_idx ON wbvr (R);" | psql favor2
