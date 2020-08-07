DROP INDEX IF EXISTS tycho2_q3c_idx;
DROP INDEX IF EXISTS tycho2_bt_idx;
DROP INDEX IF EXISTS tycho2_vt_idx;

CREATE INDEX tycho2_q3c_idx ON tycho2 (q3c_ang2ipix(ra,dec));
CREATE INDEX tycho2_bt_idx ON tycho2 (bt);
CREATE INDEX tycho2_vt_idx ON tycho2 (vt);

CLUSTER tycho2_q3c_idx ON tycho2;

ANALYZE tycho2;
