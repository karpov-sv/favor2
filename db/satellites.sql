-- Satellites
DROP TABLE IF EXISTS satellites CASCADE;
DROP SEQUENCE IF EXISTS satellites_id_seq;
CREATE SEQUENCE satellites_id_seq;
CREATE TABLE satellites (
       id INT PRIMARY KEY DEFAULT nextval('satellites_id_seq'), -- internal id, unique
       catalogue INT, -- Identifier of a catalogue: 1 - NORAD, 2 - Russian, 3 - McCants
       catalogue_id INT, -- Satellite ID according to the catalogue
       UNIQUE(catalogue, catalogue_id),
       name TEXT, -- Name
       iname TEXT, -- International Name
       country TEXT, -- Country code
       type INT DEFAULT 0, -- Satellite type

       launch_date TIMESTAMP,

       variability INT DEFAULT 0, -- Variability flag, 0 - constant, 1 - variable, 2 - periodic
       variability_period FLOAT,

       orbit_inclination FLOAT,
       orbit_period FLOAT,
       orbit_eccentricity FLOAT,

       rcs FLOAT, -- Radar Cross-Section

       comments TEXT,

       params hstore
);
CREATE INDEX ON satellites (name);
CREATE INDEX ON satellites (country);
CREATE INDEX ON satellites (catalogue);
CREATE INDEX ON satellites (catalogue_id);
CREATE INDEX ON satellites (comments);
CREATE INDEX ON satellites (type);
CREATE INDEX ON satellites (orbit_inclination);
CREATE INDEX ON satellites (orbit_period);
CREATE INDEX ON satellites (orbit_eccentricity);

-- Satellite tracks
DROP TABLE IF EXISTS satellite_tracks CASCADE;
CREATE TABLE satellite_tracks (
       id INT PRIMARY KEY, -- object_id of first record?
       satellite_id INT REFERENCES satellites(id) ON DELETE CASCADE,
       tle TEXT,

       age FLOAT, -- TLE age
       transversal_shift FLOAT,
       transversal_rms FLOAT,
       binormal_shift FLOAT,
       binormal_rms FLOAT,

       variability INT DEFAULT 0, -- Variability flag, 0 - constant, 1 - variable, 2 - periodic
       variability_period FLOAT
);
CREATE INDEX ON satellite_tracks (satellite_id);

-- Satellite records
DROP TABLE IF EXISTS satellite_records CASCADE;
DROP SEQUENCE IF EXISTS satellite_records_id_seq;
CREATE SEQUENCE satellite_records_id_seq;
CREATE TABLE satellite_records (
       id INT PRIMARY KEY DEFAULT nextval('satellite_records_id_seq'),
       --
       satellite_id INT REFERENCES satellites(id) ON DELETE CASCADE,
       track_id INT REFERENCES satellite_tracks(id) ON DELETE CASCADE,
       record_id INT REFERENCES realtime_records(id) ON DELETE CASCADE,
       object_id INT REFERENCES realtime_objects(id) ON DELETE CASCADE,
       -- Realtime parameters (denormalization)
       time TIMESTAMP,
       ra FLOAT,
       dec FLOAT,
       filter INT REFERENCES filters(id) ON DELETE CASCADE,
       mag FLOAT, -- Apparent magnitude
       channel_id INT,
       flags INT,
       -- Derived parameters
       stdmag FLOAT, -- Standard magnitude
       phase FLOAT,
       distance FLOAT,
       penumbra INT
);
CREATE INDEX ON satellite_records (satellite_id);
CREATE INDEX ON satellite_records (track_id);
CREATE INDEX ON satellite_records (record_id);
CREATE INDEX ON satellite_records (time);
CREATE INDEX ON satellite_records (penumbra);
CREATE INDEX ON satellite_records (filter);

-- Materialized view to speed up common lookups on satellites.
-- Should be updated manually, using REFRESH MATERIALIZED VIEW command
-- DROP VIEW IF EXISTS satellites_view;
DROP MATERIALIZED VIEW IF EXISTS satellites_view;
CREATE MATERIALIZED VIEW satellites_view AS (
-- CREATE VIEW satellites_view AS (
       SELECT s.*,
       (SELECT count(*) AS ntracks FROM satellite_tracks WHERE satellite_id = s.id),
       (SELECT count(*) AS nrecords FROM satellite_records WHERE satellite_id = s.id),
       (SELECT (count(*) > 0) AS penumbra FROM satellite_records WHERE satellite_id = s.id AND penumbra = 1),
       (SELECT avg(stdmag) AS mean_clear FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 0),
       (SELECT avg(stdmag) AS mean_b FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 1),
       (SELECT avg(stdmag) AS mean_v FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 2),
       (SELECT avg(stdmag) AS mean_r FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 3),
       (SELECT stddev(stdmag) AS sigma_clear FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 0),
       (SELECT stddev(stdmag) AS sigma_b FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 1),
       (SELECT stddev(stdmag) AS sigma_v FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 2),
       (SELECT stddev(stdmag) AS sigma_r FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 3),

       (SELECT median(stdmag order by stdmag) AS median_clear FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 0),
       (SELECT median(stdmag order by stdmag) AS median_b FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 1),
       (SELECT median(stdmag order by stdmag) AS median_v FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 2),
       (SELECT median(stdmag order by stdmag) AS median_r FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 3),

       (SELECT min(stdmag) AS min_clear FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 0),
       (SELECT min(stdmag) AS min_b FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 1),
       (SELECT min(stdmag) AS min_v FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 2),
       (SELECT min(stdmag) AS min_r FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 3),

       (SELECT max(stdmag) AS max_clear FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 0),
       (SELECT max(stdmag) AS max_b FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 1),
       (SELECT max(stdmag) AS max_v FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 2),
       (SELECT max(stdmag) AS max_r FROM satellite_records WHERE satellite_id = s.id AND penumbra = 0 AND filter = 3),

       (SELECT max(time) AS time_last FROM satellite_records WHERE satellite_id = s.id),
       (SELECT min(time) AS time_first FROM satellite_records WHERE satellite_id = s.id)
       FROM satellites s
);
CREATE UNIQUE INDEX ON satellites_view (id);
