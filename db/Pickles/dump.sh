#!/bin/sh

echo "DROP TABLE IF EXISTS pickles;" | psql favor2

echo "CREATE TABLE pickles (ra FLOAT, dec FLOAT, B FLOAT, V FLOAT, R FLOAT, Bt FLOAT, eBt FLOAT, Vt FLOAT, eVt FLOAT, Rn FLOAT, J FLOAT, eJ FLOAT, H FLOAT, eH FLOAT, K FLOAT, eK FLOAT, rank INT, chi2 FLOAT, var INT DEFAULT 0);" | psql favor2
#echo "CREATE TABLE pickles (ra FLOAT, dec FLOAT, B FLOAT, V FLOAT, R FLOAT, Bt FLOAT, eBt FLOAT, Vt FLOAT, eVt FLOAT, );" | psql favor2

awk '{if($1>0) print $1","$2","$25","$26","$27","$5","$6","$7","$8","$9","$10","$11","$12","$13","$14","$15","$17","$18",0"}' Tycho2Fit.dat | psql favor2 -c "COPY pickles FROM stdin WITH DELIMITER AS ','"

echo "CREATE INDEX pickles_q3c_idx ON pickles (q3c_ang2ipix(ra,dec));" | psql favor2
echo "CREATE INDEX pickles_B_idx ON pickles (B);" | psql favor2
echo "CREATE INDEX pickles_V_idx ON pickles (V);" | psql favor2
echo "CREATE INDEX pickles_R_idx ON pickles (R);" | psql favor2

echo "UPDATE pickles p SET var = 1 FROM vsx v WHERE q3c_join(v.ra, v.dec, p.ra, p.dec, 0.01);" | psql favor2
