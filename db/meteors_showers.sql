DROP TABLE IF EXISTS meteors_showers;
CREATE TABLE meteors_showers (
       id SERIAL PRIMARY KEY,
       iaucode TEXT,
       name TEXT,
       activity TEXT,
       status INT,
       solar_lon FLOAT,
       ra FLOAT,
       dec FLOAT
);

CREATE INDEX ON meteors_showers (iaucode);
CREATE INDEX ON meteors_showers (name);
CREATE INDEX ON meteors_showers (solar_lon);

DROP TABLE IF EXISTS meteors_showers_map;
CREATE TABLE meteors_showers_map (
       meteor_id INT,
       shower_id INT,
       UNIQUE(meteor_id,shower_id),
       dist FLOAT
);
CREATE INDEX ON meteors_showers_map (meteor_id);
CREATE INDEX ON meteors_showers_map (shower_id);
