-- Log of various messages and events
DROP TABLE IF EXISTS log CASCADE;
CREATE TABLE log (
       time TIMESTAMP,
       id TEXT,
       text TEXT
);
CREATE INDEX log_time_idx ON log(time);
CREATE INDEX log_id_idx ON log(id);

CREATE EXTENSION hstore;

-- Filters
DROP TABLE IF EXISTS filters CASCADE;
CREATE TABLE filters (
       id INT PRIMARY KEY,
       name TEXT
);

INSERT INTO filters (id, name) VALUES (0, 'Clear');
INSERT INTO filters (id, name) VALUES (1, 'B');
INSERT INTO filters (id, name) VALUES (2, 'V');
INSERT INTO filters (id, name) VALUES (3, 'R');
INSERT INTO filters (id, name) VALUES (4, 'Pol');
INSERT INTO filters (id, name) VALUES (5, 'B+Pol');
INSERT INTO filters (id, name) VALUES (6, 'V+Pol');
INSERT INTO filters (id, name) VALUES (7, 'R+Pol');

CREATE OR REPLACE FUNCTION get_filter_name (iid INT)
RETURNS TEXT AS $$
SELECT name FROM filters WHERE id = iid;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION get_filter_id (iname TEXT)
RETURNS INT AS $$
SELECT id from filters where name = iname;
$$ LANGUAGE SQL;

-- Image storage metadata
DROP TABLE IF EXISTS images CASCADE;
CREATE TABLE images (
       id SERIAL PRIMARY KEY,
       channel_id INT,
       filename TEXT,
       night TEXT,
       time TIMESTAMP,
       type TEXT,
       filter INT REFERENCES filters (id),
       ra0 FLOAT,
       dec0 FLOAT,
       width INT,
       height INT,
       keywords hstore
);
CREATE INDEX images_filename_idx ON images(filename);
CREATE INDEX images_night_idx ON images(night);
CREATE INDEX images_time_idx ON images(time);
CREATE INDEX images_type_idx ON images(type);
CREATE INDEX images_keywords_idx ON images USING BTREE(keywords);
CREATE INDEX images_q3c_idx ON images (q3c_ang2ipix(ra0, dec0));
CREATE INDEX ON images (night) WHERE type = 'avg';

-- Find latest image of a given type before given moment of time
CREATE OR REPLACE FUNCTION find_image (t TIMESTAMP DEFAULT favor2_timestamp(), tp TEXT DEFAULT 'dark', ch_id INT DEFAULT -1)
RETURNS TEXT AS $$
SELECT i.filename FROM images i WHERE i.type=tp AND i.time < t AND (ch_id < 0 OR i.channel_id = ch_id) ORDER BY age(t, i.time) ASC LIMIT 1;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION find_image_id (t TIMESTAMP DEFAULT favor2_timestamp(), tp TEXT DEFAULT 'dark', ch_id INT DEFAULT -1)
RETURNS INT AS $$
SELECT i.id FROM images i WHERE i.type=tp and i.time < t AND (ch_id < 0 OR i.channel_id = ch_id) ORDER BY age(t, i.time) ASC LIMIT 1;
$$ LANGUAGE SQL;
