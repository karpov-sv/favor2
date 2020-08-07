-- DROP TABLE IF EXISTS scheduler_targets;
-- DROP SEQUENCE IF EXISTS scheduler_targets_id_seq;
-- CREATE SEQUENCE scheduler_targets_id_seq;
-- -- Targets of interest for the scheduler
-- CREATE TABLE scheduler_targets (
--        id INT PRIMARY KEY DEFAULT nextval('scheduler_targets_id_seq'),
--        name TEXT,
--        ra0 FLOAT,
--        dec0 FLOAT,
--        sr0  FLOAT DEFAULT 0,
--        time TSRANGE,
--        weight FLOAT
-- );
-- CREATE INDEX scheduler_targets_name_idx ON scheduler_targets(name);
-- CREATE INDEX scheduler_targets_time_idx ON scheduler_targets USING gist(time);
-- CREATE INDEX scheduler_targets_q3c_idx ON scheduler_targets(q3c_ang2ipix(ra0, dec0));;

-- Permanent storage of a coverage per night
DROP TABLE IF EXISTS scheduler_coverage CASCADE;
CREATE TABLE scheduler_coverage (
       night TEXT PRIMARY KEY,
       time TIMESTAMP,
       coverage BYTEA,
       smooth BYTEA
);

-- Plan status
DROP TABLE IF EXISTS scheduler_target_status CASCADE;
create table scheduler_target_status (
       id INT PRIMARY KEY,
       name TEXT
);

INSERT INTO scheduler_target_status (id, name) VALUES (1, 'inactive'); -- Will not be considered for scheduling
INSERT INTO scheduler_target_status (id, name) VALUES (2, 'active'); -- Will be scheduled when possible
INSERT INTO scheduler_target_status (id, name) VALUES (3, 'completed'); -- Observations completed
INSERT INTO scheduler_target_status (id, name) VALUES (4, 'archived'); -- Results archived or sent to the user
INSERT INTO scheduler_target_status (id, name) VALUES (5, 'failed'); -- Observations failed

CREATE OR REPLACE FUNCTION get_scheduler_target_status_id (TEXT)
RETURNS INT AS $$
SELECT id from scheduler_target_status WHERE name = $1;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION get_scheduler_target_status (INT)
RETURNS TEXT AS $$
SELECT name from scheduler_target_status WHERE id = $1;
$$ LANGUAGE SQL;

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Observing targets
DROP TABLE IF EXISTS scheduler_targets CASCADE;
CREATE TABLE scheduler_targets (
       id SERIAL PRIMARY KEY,
       external_id INT,
       name TEXT,
       type TEXT,
       ra FLOAT,
       dec FLOAT,
       exposure FLOAT,
       filter TEXT,
       repeat INT DEFAULT 1,
       status INT DEFAULT 2 REFERENCES scheduler_target_status(id) ON DELETE CASCADE,
       time_created TIMESTAMP DEFAULT current_timestamp AT TIME ZONE 'UTC',
       time_completed TIMESTAMP,
       timetolive FLOAT DEFAULT 0,
       priority FLOAT DEFAULT 1,
       uuid TEXT UNIQUE DEFAULT uuid_generate_v4(),
       timetolive FLOAT DEFAULT 0,
       -- Everything else
       params hstore
);

-- Followups
DROP TABLE IF EXISTS scheduler_followup_status CASCADE;
create table scheduler_followup_status (
       id INT PRIMARY KEY,
       name TEXT
);

INSERT INTO scheduler_followup_status (id, name) VALUES (1, 'active'); -- Will be considered for scheduling
INSERT INTO scheduler_followup_status (id, name) VALUES (2, 'completed'); -- Observations completed
INSERT INTO scheduler_followup_status (id, name) VALUES (2, 'archived');  -- Results archived or sent to the user
INSERT INTO scheduler_followup_status (id, name) VALUES (3, 'failed'); -- Observations failed

CREATE OR REPLACE FUNCTION get_scheduler_followup_status_id (TEXT)
RETURNS INT AS $$
SELECT id from scheduler_followup_status WHERE name = $1;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION get_scheduler_followup_status (INT)
RETURNS TEXT AS $$
SELECT name from scheduler_followup_status WHERE id = $1;
$$ LANGUAGE SQL;

DROP TABLE IF EXISTS scheduler_followups CASCADE;
CREATE TABLE scheduler_followups (
       id SERIAL PRIMARY KEY,
       external_id INT,
);
