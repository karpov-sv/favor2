DROP VIEW IF EXISTS meteors_view CASCADE;
CREATE VIEW meteors_view AS (
       SELECT id, channel_id, night, time_start as time, time_end,
       (params->'ra_start')::FLOAT AS ra_start,
       (params->'dec_start')::FLOAT AS dec_start,
       (params->'ra_end')::FLOAT AS ra_end,
       (params->'dec_end')::FLOAT AS dec_end,
       (params->'nframes')::INT AS nframes,
       (params->'ang_vel')::FLOAT AS ang_vel,
       (params->'z_start')::FLOAT AS z,
       (params->'mag_calibrated')::INT AS mag_calibrated,
       (params->'mag0')::FLOAT AS mag0,
       (params->'mag_min')::FLOAT AS mag_min,
       (params->'mag_max')::FLOAT AS mag_max,
       (params->'filter')::TEXT AS filter
       FROM realtime_objects
       WHERE state = 0 AND (params->'analyzed') = '1'
);

DROP VIEW IF EXISTS meteors_nights_view CASCADE;
CREATE VIEW meteors_nights_view AS (
       SELECT night,
       count(*) AS nmeteors
       FROM meteors_view
       GROUP BY night
);

DROP VIEW IF EXISTS meteors_nights_images_view CASCADE;
CREATE VIEW meteors_nights_images_view AS (
       SELECT night,
       count(*) AS nmeteors,
       (SELECT count(*) FROM images WHERE night=meteors_view.night AND type='avg') AS nimages
       FROM meteors_view
       GROUP BY night
);

DROP TABLE IF EXISTS meteors_nights_processed;
CREATE TABLE meteors_nights_processed (
       night TEXT PRIMARY KEY,
       comments TEXT,
       params hstore
);
