--- Status of the system
DROP TABLE beholder_status CASCADE;
create table beholder_status (
       id SERIAL PRIMARY KEY,
       time TIMESTAMP,
       status hstore
);
CREATE INDEX ON beholder_status (time);
