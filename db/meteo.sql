-- All-sky images
DROP TABLE IF EXISTS meteo_images CASCADE;
CREATE TABLE meteo_images (
       id SERIAL PRIMARY KEY,
       filename TEXT,
       night TEXT,
       time TIMESTAMP,
       exposure FLOAT
);
CREATE INDEX meteo_images_filename_idx ON meteo_images(filename);
CREATE INDEX meteo_images_night_idx ON meteo_images(night);
CREATE INDEX meteo_images_time_idx ON meteo_images(time);

-- Meteo parameters
DROP TABLE IF EXISTS meteo_parameters CASCADE;
CREATE TABLE meteo_parameters (
       id SERIAL PRIMARY KEY,
       time TIMESTAMP,
       sensor_temp FLOAT,
       sky_ambient_temp FLOAT,
       ambient_temp FLOAT,
       wind FLOAT,
       humidity FLOAT,
       dewpoint FLOAT,

       rain_flag INT,
       wet_flag INT,

       cloud_cond INT,
       wind_cond INT,
       rain_cond INT,
       day_cond INT
);
CREATE INDEX meteo_parameters_time_idx ON meteo_parameters(time);

-- Sky Quality Monitor
DROP TABLE IF EXISTS meteo_sqm CASCADE;
CREATE TABLE meteo_sqm (
       id SERIAL PRIMARY KEY,
       time TIMESTAMP,
       temp FLOAT,
       brightness FLOAT
);
CREATE INDEX meteo_sqm_time_idx ON meteo_sqm(time);
