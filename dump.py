#!/usr/bin/env python
import os
import sys
import posixpath
import zipfile
import StringIO
import numpy as np

from favor2 import Favor2

def dump_satellites(favor2, night='all', id=0, state='moving', max_age=0, min_length=0, outdir='out', crlf=False, use_zip=False):
    where = [];
    where_opts = [];

    if state and state != 'all':
        where.append("state = get_realtime_object_state_id(%s)")
        where_opts.append(state)

    if night and night != 'all':
        where.append("night = %s")
        where_opts.append(night)

    if id:
        where.append("id = %s")
        where_opts.append(id)

    if max_age:
        where.append("time_end > current_timestamp at time zone 'utc' - '%s seconds'::interval")
        where_opts.append(max_age)

    where_string = " AND ".join(where)
    if where_string:
        where_string = "WHERE " + where_string

    res = favor2.query("SELECT *, get_realtime_object_state(state) as state_string FROM realtime_objects %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

    if res:
        if use_zip:
            string = StringIO.StringIO()
            zip = zipfile.ZipFile(string, "w")
        else:
            try:
                os.makedirs(outdir)
            except:
                pass

    for obj in res:
        records = favor2.query("SELECT * FROM realtime_records WHERE object_id = %s ORDER BY time;", (obj['id'],), simplify=False)
        is_good = True

        if len(records) > min_length and is_good:
            if use_zip:
                file = StringIO.StringIO()
            else:
                print obj['id'], obj['state'], len(records)

                filename = posixpath.join(outdir, "%s_%s_%d.txt" %(obj['night'], obj['state_string'], obj['id']))
                file = open(filename, "w")

            for r in records:
                print >>file, r['time'], r['ra'], r['dec'], r['mag'], r['x'], r['y'], obj['channel_id'], r['filter'], r['flags'], ("\r" if crlf else "")

            if use_zip:
                zip.writestr("%s_%s_%d.txt" %(obj['night'], obj['state_string'], obj['id']), file.getvalue())

            file.close()

    if res and use_zip:
        zip.close()
        return string.getvalue()

def dump_meteors(favor2, night='all', state='meteor', crlf=False, skip_comments=True):
    where = ["params->'analyzed' = '1'"];
    where_opts = [];

    if state and state != 'all':
        where.append("state = get_realtime_object_state_id(%s)")
        where_opts.append(state)

    if night and night != 'all':
        where.append("night = %s")
        where_opts.append(night)

    if skip_comments:
        where.append("id NOT IN (SELECT object_id FROM realtime_comments)")

    where_string = " AND ".join(where)
    if where_string:
        where_string = "WHERE " + where_string

    res = favor2.query("SELECT *, params->'ra_start' as ra_start, params->'dec_start' as dec_start, params->'ra_end' as ra_end, params->'dec_end' as dec_end, params->'nframes' as nframes, get_realtime_object_state(state) as state_string FROM realtime_objects %s ORDER BY time_start;" % (where_string), where_opts, simplify=False)

    for obj in res:
        print obj['id'], obj['time_start'], obj['ra_start'], obj['dec_start'], obj['ra_end'], obj['dec_end'], obj['nframes'], obj['channel_id'], ("\r" if crlf else "")

# Main
if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-d', '--dbname', help='database name, defaults to favor2', action='store', dest='dbname', default='favor2')
    parser.add_option('-H', '--dbhost', help='database hostname, defaults to none', action='store', dest='dbhost', default='')
    parser.add_option('-s', '--state', help='Object state, default to all', action='store', dest='state', default='')
    parser.add_option('-n', '--night', help='Night to process, defaults to all', action='store', dest='night', default='')
    parser.add_option('-l', '--min-length', help='Minimal object length for filtering, defaults to 100', action='store', dest='min_length', default=100)
    parser.add_option('-o', '--outdir', help='Output directory, defaults to out', action='store', dest='outdir', default='out')
    parser.add_option('--crlf', help='Use CRLF', action='store_true', dest='crlf', default=False)
    parser.add_option('-m', '--meteors', help='Dump meteor trails', action='store_true', dest='dump_meteors', default=False)

    (options, args) = parser.parse_args()

    f = Favor2(dbname=options.dbname, dbhost=options.dbhost)

    if options.dump_meteors:
        dump_meteors(f, night=options.night, crlf=options.crlf)
    else:
        dump_satellites(f, night=options.night, state=options.state, min_length=int(options.min_length), outdir=options.outdir, crlf=options.crlf)
