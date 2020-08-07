-- Master index of all stationary objects
DROP TABLE IF EXISTS stars CASCADE;
CREATE TABLE stars (
       id SERIAL PRIMARY KEY,
       -- Position on the sky
       ra0 FLOAT,
       dec0 FLOAT,
       -- All other params
       params hstore
);
CREATE INDEX stars_q3c_idx ON stars (q3c_ang2ipix(ra0, dec0));

-- All measurements, linked to stars through star_id field
DROP TABLE IF EXISTS records CASCADE;
CREATE TABLE records (
       id SERIAL PRIMARY KEY,
       -- Channel id
       channel_id INT,
       -- Timestamp of the frame
       time TIMESTAMP,
       -- Filter used
       filter INT REFERENCES filters (id),
       -- Position on the sky
       ra FLOAT,
       dec FLOAT,
       -- Magnitude
       mag FLOAT,
       mag_err FLOAT,
       -- Color equation
       Cbv FLOAT,
       Cvr FLOAT,
       quality FLOAT,
       -- Flux
       flux FLOAT,
       flux_err FLOAT,
       -- Frame
       frame INT REFERENCES images (id),
       -- Position on the original frame
       x FLOAT,
       y FLOAT,
       -- Flags
       flags INT
);
CREATE INDEX ON records (q3c_ang2ipix(ra, dec));
CREATE INDEX ON records (channel_id);
CREATE INDEX ON records (time);
CREATE INDEX ON records (filter);
CREATE INDEX ON records (flags);

DROP TABLE IF EXISTS frames CASCADE;
CREATE TABLE frames (
       id INT UNIQUE REFERENCES images(id) ON DELETE CASCADE,
       -- Channel id
       channel_id INT,
       -- Timestamp of the frame
       time TIMESTAMP,
       -- Filter used
       filter INT REFERENCES filters (id),
       -- Center position on the sky
       ra0 FLOAT,
       dec0 FLOAT,
       -- Color equation
       Cbv FLOAT,
       Cvr FLOAT,
       -- Quality
       quality FLOAT,
       -- All other params
       params hstore
);
CREATE INDEX ON frames (q3c_ang2ipix(ra0, dec0));
CREATE INDEX ON frames (channel_id);
CREATE INDEX ON frames (time);
CREATE INDEX ON frames (filter);

-- CREATE OR REPLACE FUNCTION get_nearest_object_id (ra1 FLOAT, dec1 FLOAT, sr1 FLOAT DEFAULT 20.0) RETURNS INT AS $$
-- DECLARE
--         id1 INT;
-- BEGIN
--         SELECT id INTO id1 FROM
--                 (SELECT id, q3c_dist(ra0, dec0, ra1, dec1) dist FROM stars WHERE
--                 q3c_radial_query(ra0, dec0, ra1, dec1, sr1/3600) ORDER BY dist ASC LIMIT 1)   AS s;

--         IF id1 IS NULL THEN
--                  INSERT INTO objects (ra0, dec0) VALUES (ra1, dec1) RETURNING id INTO id1;
--         END IF;

--         RETURN id1;
-- END;
-- $$ LANGUAGE plpgsql;

-- CREATE OR REPLACE FUNCTION insert_measurement (ra1 FLOAT, dec1 FLOAT, sr1 FLOAT, instr1 FLOAT DEFAULT 0, mag1 FLOAT DEFAULT 0)
-- RETURNS INT AS $$
-- DECLARE
--         id1 INT;
-- BEGIN
--         INSERT INTO measurements (ra, dec, instr, mag, star_id) VALUES (ra1, dec1, instr1, mag1, get_nearest_star_id(ra1, dec1, sr1)) RETURNING star_id INTO id1;
--         RETURN id1;
-- END;
-- $$ LANGUAGE plpgsql;
