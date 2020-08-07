#!/bin/sh

echo "DROP TABLE IF EXISTS heiles;" | psql favor2

echo "CREATE TABLE heiles (HD INT, ra FLOAT, dec FLOAT, V FLOAT, Pol FLOAT, e_Pol FLOAT, PA FLOAT, e_PA FLOAT);" | psql favor2

awk '{if($2*1>0) print ($3*1)","$2","$1","$11","$4","$5","$6","$7}' heiles.txt | sed -e 's/""/NULL/g' | psql favor2 -c "COPY heiles FROM stdin WITH DELIMITER AS ',' NULL AS 'NULL'"

echo "CREATE INDEX heiles_q3c_idx ON heiles (q3c_ang2ipix(ra,dec));" | psql favor2
echo "CREATE INDEX heiles_Pol_idx ON heiles (Pol);" | psql favor2
echo "CREATE INDEX heiles_PA_idx ON heiles (PA);" | psql favor2
echo "CREATE INDEX heiles_V_idx ON heiles (V);" | psql favor2
