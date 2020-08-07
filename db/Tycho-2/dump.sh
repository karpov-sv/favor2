#!/bin/sh

psql favor2 < tycho2.sql

./dump.py | psql favor2 -c "copy tycho2 from stdin with delimiter as '	'"

# name, ra, dec, Bt, Vt
# awk 'BEGIN{FS="|";}{
# name=$1;
# q=$2;
# ra=$3;
# dec=$4;
# Bt=$18*1;
# Vt=$20*1;
# if(Bt==0 && Vt!=0) Bt=Vt;
# if(Vt==0 && Bt!=0) Vt=Bt;

# if(q=="X"){ra=$25;dec=$26;}
# print name"\t"ra"\t"dec"\t"Bt"\t"Vt}' data/catalog.dat|psql favor2 -c "copy tycho2 from stdin with delimiter as '	'"

# awk 'BEGIN{FS="|";}{
# name=$1;
# ra=$3;
# dec=$4;
# Bt=$12*1;
# Vt=$14*1;
# if(Bt==0 && Vt!=0) Bt=Vt;
# if(Vt==0 && Bt!=0) Vt=Bt;
# print name"\t"ra"\t"dec"\t"Bt"\t"Vt}' data/suppl_1.dat|psql favor2 -c "copy tycho2 from stdin with delimiter as '	'"

# awk 'BEGIN{FS="|";}{
# name=$1;
# ra=$3;
# dec=$4;
# Bt=$12*1;
# Vt=$14*1;
# if(Bt==0 && Vt!=0) Bt=Vt;
# if(Vt==0 && Bt!=0) Vt=Bt;
# print name"\t"ra"\t"dec"\t"Bt"\t"Vt}' data/suppl_2.dat|psql favor2 -c "copy tycho2 from stdin with delimiter as '	'"

psql favor2 < tycho2-idx.sql
