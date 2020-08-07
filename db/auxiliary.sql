-- Current timestamp, UTC without time zone
CREATE OR REPLACE FUNCTION favor2_timestamp ()
RETURNS TIMESTAMP AS $$
SELECT current_timestamp AT TIME ZONE 'UTC';
$$ LANGUAGE SQL;

-- Convert 'timestamp with time zone' to 'timestamp' by dropping the time zone
CREATE OR REPLACE FUNCTION favor2_remove_time_zone (t TIMESTAMP WITH TIME ZONE)
RETURNS TIMESTAMP AS $$
SELECT t::TIMESTAMP WITHOUT TIME ZONE;
$$ LANGUAGE SQL;

-- UTC to local time
CREATE OR REPLACE FUNCTION favor2_utc_to_local (t TIMESTAMP DEFAULT favor2_timestamp())
RETURNS TIMESTAMP AS $$
select t - age(now() AT TIME ZONE 'UTC', now());
$$ LANGUAGE SQL;

-- local time to UTC
CREATE OR REPLACE FUNCTION favor2_local_to_utc (t TIMESTAMP DEFAULT favor2_timestamp())
RETURNS TIMESTAMP AS $$
select t + age(now() AT TIME ZONE 'UTC', now());
$$ LANGUAGE SQL;

-- Night name from timestamp
CREATE OR REPLACE FUNCTION favor2_night (t TIMESTAMP DEFAULT favor2_timestamp())
RETURNS TEXT AS $$
SELECT to_char(favor2_utc_to_local(t) - interval '12 hours', 'YYYY_MM_DD');
$$ LANGUAGE SQL;

-- Interval of time corresponding to the night
CREATE OR REPLACE FUNCTION favor2_night_tsrange (night TEXT DEFAULT favor2_night())
RETURNS TSRANGE AS
$$
SELECT tsrange(t0 + INTERVAL '12 hours', t0 + INTERVAL '36 hour')
       FROM favor2_local_to_utc(favor2_remove_time_zone(to_timestamp(night, 'YYYY_MM_DD'))) AS t0;
$$ LANGUAGE SQL;

-- Time interval from start and (optional) duration
CREATE OR REPLACE FUNCTION favor2_tsrange(t1 TIMESTAMP, deltat INTERVAL DEFAULT '1 second')
RETURNS TSRANGE AS
$$
SELECT tsrange(t1, t1 + deltat);
$$ LANGUAGE SQL;

-- Fast DISTINCT using the index
CREATE OR REPLACE FUNCTION fast_distinct(
   tableName varchar, fieldName varchar, sample anyelement = ''::varchar)
   -- Search a few distinct values in a possibly huge table
   -- Parameters: tableName or query expression, fieldName,
   --             sample: any value to specify result type (defaut is varchar)
   -- Author: T.Husson, 2012-09-17, distribute/use freely
   RETURNS TABLE ( result anyelement ) AS
$BODY$
BEGIN
   EXECUTE 'SELECT '||fieldName||' FROM '||tableName||' ORDER BY '||fieldName
      ||' LIMIT 1'  INTO result;
   WHILE result IS NOT NULL LOOP
      RETURN NEXT;
      EXECUTE 'SELECT '||fieldName||' FROM '||tableName
         ||' WHERE '||fieldName||' > $1 ORDER BY ' || fieldName || ' LIMIT 1'
         INTO result USING result;
   END LOOP;
END;
$BODY$ LANGUAGE plpgsql VOLATILE;
