DROP TABLE IF EXISTS survey_transients CASCADE;
CREATE TABLE survey_transients (
       id SERIAL PRIMARY KEY,
       -- Channel id
       channel_id INT,
       -- Frame id
       frame_id INT REFERENCES images (id) ON DELETE CASCADE,
       -- Timestamp of the frame
       time TIMESTAMP,
       night TEXT,
       -- Filter used
       filter INT REFERENCES filters (id),
       -- Position on the sky
       ra FLOAT,
       dec FLOAT,
       -- Magnitude
       mag FLOAT,
       mag_err FLOAT,
       -- Flux
       flux FLOAT,
       flux_err FLOAT,
       -- Position on the original frame
       x FLOAT,
       y FLOAT,
       -- -- Ellipse parameters
       -- a FLOAT,
       -- b FLOAT,
       -- theta FLOAT,
       -- Flags
       flags INT,
       -- Preview
       preview TEXT,
       -- SIMBAD
       simbad TEXT,
       mpc TEXT,
       -- Misc
       params hstore
);

CREATE INDEX ON survey_transients (channel_id);
CREATE INDEX ON survey_transients (frame_id);
CREATE INDEX ON survey_transients (time);
CREATE INDEX ON survey_transients (q3c_ang2ipix(ra, dec));
