CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

DROP TABLE IF EXISTS targets CASCADE;
DROP SEQUENCE IF EXISTS targets_id_seq;
CREATE SEQUENCE targets_id_seq;
CREATE TABLE targets(
       id INT PRIMARY KEY DEFAULT nextval('targets_id_seq'),
       name  TEXT,
       ra    FLOAT,
       dec   FLOAT,
       type  TEXT
);
CREATE INDEX ON targets(name);

DROP TABLE IF EXISTS plan_status CASCADE;
CREATE TABLE plan_status(
       id INT PRIMARY KEY,
       name TEXT
);

INSERT INTO plan_status (id, name) VALUES (1, 'advertised');
INSERT INTO plan_status (id, name) VALUES (2, 'approved');
INSERT INTO plan_status (id, name) VALUES (3, 'confirmed');
INSERT INTO plan_status (id, name) VALUES (4, 'rejected');
INSERT INTO plan_status (id, name) VALUES (5, 'accepted');
INSERT INTO plan_status (id, name) VALUES (6, 'completed');
INSERT INTO plan_status (id, name) VALUES (7, 'cancelled');
INSERT INTO plan_status (id, name) VALUES (8, 'deleted');

CREATE OR REPLACE FUNCTION get_status_id (TEXT)
RETURNS INT AS $$
SELECT id from plan_status WHERE name = $1;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION get_status (INT)
RETURNS TEXT AS $$
SELECT name from plan_status WHERE id = $1;
$$ LANGUAGE SQL;

DROP TABLE IF EXISTS plans CASCADE;
DROP SEQUENCE IF EXISTS plans_id_seq;
CREATE SEQUENCE plans_id_seq;
CREATE TABLE plans(
       id INT PRIMARY KEY DEFAULT nextval('plans_id_seq'),
       target_id INT REFERENCES targets(id) ON DELETE CASCADE,
       status INT REFERENCES plan_status(id) ON DELETE CASCADE,
       time_start TIMESTAMP WITHOUT TIME ZONE DEFAULT '1970-01-01',
       time_end TIMESTAMP WITHOUT TIME ZONE DEFAULT '2070-01-01',
       exposure FLOAT,
       min_altitude FLOAT DEFAULT 30,
       max_moon_altitude FLOAT DEFAULT 30,
       min_moon_distance FLOAT DEFAULT 30,
       filter TEXT,
       uuid TEXT UNIQUE DEFAULT uuid_generate_v4()
);
CREATE INDEX ON plans(status);

DROP TABLE IF EXISTS images CASCADE;
DROP SEQUENCE IF EXISTS images_id_seq;
CREATE SEQUENCE images_id_seq;
CREATE TABLE images(
       id INT PRIMARY KEY DEFAULT nextval('images_id_seq'),
       plan_id INT REFERENCES plans(id) on delete cascade,
       filename TEXT,
       url TEXT,
       preview_url TEXT,
       type TEXT DEFAULT 'raw',
       UNIQUE(plan_id, filename, url)
);
CREATE INDEX ON images(type);
