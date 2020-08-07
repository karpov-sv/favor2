-- Populate scheduler_targets
INSERT INTO scheduler_targets (ra0, dec0, name, time, weight) (SELECT random()*360 AS ra0, acos(1 - 2*random())/pi()*180 - 90 AS dec0, 'test'::text AS name, favor2_night_tsrange() AS time, random() as weight  FROM generate_series(1,10));


-- Grouping of images close in time and space
WITH s AS (SELECT filename,time CASE WHEN ((time-lag(time) OVER w BETWEEN '1 second'::interval AND '30 seconds'::interval) AND q3c_dist(ra0, dec0, lag(ra0) OVER w, lag(dec0) OVER w) < 1) THEN 0 ELSE 1 END AS flag FROM images WHERE type='avg' WINDOW w AS (ORDER BY time)),
grp AS (SELECT *,sum(flag) OVER (ORDER BY time) AS gid FROM s)
SELECT count(filename) AS length, min(filename) AS first, min(night) as night, array_agg(filename) as filenames FROM grp GROUP BY gid ORDER BY time;



WITH s AS (
     SELECT filename,time,night,channel_id, CASE WHEN ((time-lag(time) OVER w BETWEEN '1 second'::interval AND '30 seconds'::interval) AND q3c_dist(ra0, dec0, lag(ra0) OVER w, lag(dec0) OVER w) < 1) THEN 0 ELSE 1 END AS flag FROM images WHERE type='avg' AND (ra0 != 0 OR dec0 != 0) WINDOW w AS (ORDER BY channel_id,time)
   ),
grp AS (
    SELECT *,sum(flag) OVER (ORDER BY time) AS gid FROM s
    )
SELECT count(filename) AS length,
       min(filename) AS first,
       min(night) as night,
       min(channel_id) as channel_id,
       array_agg(filename) as filenames,
       min(time) as time FROM grp GROUP BY gid;

-- Search for meteors due to bright stars
select * from (select id, (select count(*) as count from realtime_records r, wbvr w where r.object_id = realtime_objects.id and q3c_radial_query(r.ra, r.dec, w.ra, w.dec, 0.05)) as count, (select count(*) from realtime_records where object_id = realtime_objects.id) as nrecords from realtime_objects where state = get_realtime_object_state_id('meteor')) t where count > 0 and nrecords = 1;


-- Hours of working
create temp table usage as select channel_id,night,count(*) from images where type='avg' group by channel_id,night order by night;
select year,count(year) as nights,round(sum(count)/36) as hours from (select distinct on (night)  night,count,substring(night for 4) as year from usage order by night) t group by year order by year;
