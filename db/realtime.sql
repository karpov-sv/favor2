-- Object status
DROP TABLE IF EXISTS realtime_object_state CASCADE;
CREATE TABLE realtime_object_state (
       id INT PRIMARY KEY,
       name TEXT
);

-- Should correspond to enum in realtime_types.h
INSERT INTO realtime_object_state (id, name) VALUES (0, 'meteor');
INSERT INTO realtime_object_state (id, name) VALUES (1, 'single');
INSERT INTO realtime_object_state (id, name) VALUES (2, 'double');
INSERT INTO realtime_object_state (id, name) VALUES (3, 'moving');
INSERT INTO realtime_object_state (id, name) VALUES (4, 'flash');
INSERT INTO realtime_object_state (id, name) VALUES (5, 'particle');
INSERT INTO realtime_object_state (id, name) VALUES (6, 'misc');
INSERT INTO realtime_object_state (id, name) VALUES (255, 'finished');

CREATE OR REPLACE FUNCTION get_realtime_object_state_id (TEXT)
RETURNS INT AS $$
SELECT id from realtime_object_state WHERE name = $1;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION get_realtime_object_state (INT)
RETURNS TEXT AS $$
SELECT name from realtime_object_state WHERE id = $1;
$$ LANGUAGE SQL;

-- Objects
DROP TABLE IF EXISTS realtime_objects CASCADE;
CREATE TABLE realtime_objects (
       id SERIAL PRIMARY KEY,
       channel_id INT,
       night TEXT,
       time_start TIMESTAMP,
       time_end TIMESTAMP,
       state INT REFERENCES realtime_object_state(id) ON DELETE CASCADE,
       ra0 FLOAT, -- Some rough position of an object-as-a-whole
       dec0 FLOAT,
       params hstore -- All other parameters
);

CREATE INDEX ON realtime_objects (night);
CREATE INDEX ON realtime_objects (time_start);
CREATE INDEX ON realtime_objects (time_end);
CREATE INDEX ON realtime_objects (state);
CREATE INDEX ON realtime_objects (q3c_ang2ipix(ra0, dec0));
CREATE INDEX ON realtime_objects USING btree ((params->'analyzed'));

-- Records
DROP TABLE IF EXISTS realtime_records CASCADE;
CREATE TABLE realtime_records (
       id SERIAL PRIMARY KEY,
       object_id INT REFERENCES realtime_objects(id) ON DELETE CASCADE,
       -- Timestamp of the frame with record
       time TIMESTAMP,
       -- Position on the sky
       ra FLOAT,
       dec FLOAT,
       -- Position on the original frame
       x FLOAT,
       y FLOAT,
       -- Ellipse parameters
       a FLOAT,
       b FLOAT,
       theta FLOAT,
       -- Flux
       flux FLOAT,
       flux_err FLOAT,
       -- Mag
       mag FLOAT,
       mag_err FLOAT,
       filter INT REFERENCES filters(id) ON DELETE CASCADE,
       -- Flags
       flags INT,
       -- All other params
       params hstore
);
CREATE INDEX ON realtime_records (object_id);
CREATE INDEX ON realtime_records (time);
-- CREATE INDEX ON realtime_records (q3c_ang2ipix(ra, dec));

-- Images
DROP TABLE IF EXISTS realtime_images CASCADE;
CREATE TABLE realtime_images (
       id SERIAL PRIMARY KEY,
       object_id INT NOT NULL REFERENCES realtime_objects(id) ON DELETE CASCADE,
       record_id INT REFERENCES realtime_records(id) ON DELETE CASCADE,
       filename TEXT NOT NULL,
       time TIMESTAMP, -- For sorting etc
       keywords hstore
);
CREATE INDEX ON realtime_images (object_id);
CREATE INDEX ON realtime_images (record_id);
CREATE INDEX ON realtime_images (time);

DROP TABLE IF EXISTS realtime_comments CASCADE;
CREATE TABLE realtime_comments (
       id SERIAL PRIMARY KEY,
       object_id INT NOT NULL REFERENCES realtime_objects(id) ON DELETE CASCADE UNIQUE,
       comment TEXT
);
CREATE INDEX ON realtime_comments (object_id);


-- Nights list
DROP VIEW IF EXISTS nights_view;
CREATE VIEW nights_view AS (
-- SELECT night,
--        count(night) AS nimages,
--        (SELECT count(*) FROM realtime_objects WHERE night=i.night) AS nobjects,
--        (SELECT count(*) FROM realtime_objects WHERE night=i.night AND state=0) AS nobjects_meteor,
--        (SELECT count(*) FROM realtime_objects WHERE night=i.night AND state=3) AS nobjects_moving,
--        (SELECT count(*) FROM realtime_objects WHERE night=i.night AND state=4) AS nobjects_flash
--        FROM images i GROUP BY night
WITH
nights AS (SELECT night, count(night) AS nimages FROM images GROUP BY night),
nights_objects AS (SELECT night, state, count(state) AS nobjects FROM realtime_objects GROUP BY night, state)
SELECT *,
(SELECT sum(nobjects) FROM nights_objects WHERE night=nights.night) AS nobjects,
(SELECT nobjects FROM nights_objects WHERE night=nights.night AND state=0) AS nobjects_meteor,
(SELECT nobjects FROM nights_objects WHERE night=nights.night AND state=3) AS nobjects_moving,
(SELECT nobjects FROM nights_objects WHERE night=nights.night AND state=4) AS nobjects_flash
FROM nights ORDER BY night DESC
);

DROP VIEW IF EXISTS nights_latest_view;
CREATE VIEW nights_latest_view AS (
WITH
nights AS (SELECT night, count(night) AS nimages FROM images GROUP BY night ORDER BY night DESC LIMIT 20),
nights_objects AS (SELECT night, state, count(state) AS nobjects FROM realtime_objects WHERE night IN (SELECT night FROM nights) GROUP BY night, state)
SELECT *,
(SELECT sum(nobjects) FROM nights_objects WHERE night=nights.night) AS nobjects,
(SELECT nobjects FROM nights_objects WHERE night=nights.night AND state=0) AS nobjects_meteor,
(SELECT nobjects FROM nights_objects WHERE night=nights.night AND state=3) AS nobjects_moving,
(SELECT nobjects FROM nights_objects WHERE night=nights.night AND state=4) AS nobjects_flash
FROM nights
);
