#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#include "utils.h"

#include "channel.h"
#include "db.h"
#include "realtime_types.h"

typedef struct {
    channel_str *channel;
    db_str *db;
} channel_db_str;

char *find_file_with_type(channel_db_str *db, char *type)
{
    if(!db->db)
        return NULL;

    char *filename = NULL;
    PGresult *res = db_query(db->db, "SELECT filename FROM images WHERE type='%s' AND channel_id = %d ORDER BY time DESC LIMIT 1;", type, db->channel->id);

    if(PQntuples(res))
        filename = db_get_char(res, 0, 0);

    PQclear(res);

    /* Check whether the file is on NAS */
    if(!file_exists_and_normal(filename)){
        char *newname = make_string("/data/%s", filename);

        free(filename);
        filename = newname;
    }

    return filename;
}

void store_image(channel_db_str *db, image_str *image)
{
    time_str evening_time = time_incremented(image->time, -12*60*60);
    char *type = image_keyword_get_string(image, "TYPE");
    char *filter = image_keyword_get_string(image, "FILTER");
    char *night = make_string("%04d_%02d_%02d", evening_time.year, evening_time.month, evening_time.day);
    char *dirname = make_string("%s/%s", db->channel->filepath, night);
    char *filename = make_string("%s/%04d%02d%02d_%02d%02d%02d_%d.%s.fits", dirname,
                                 image->time.year, image->time.month, image->time.day,
                                 image->time.hour, image->time.minute, image->time.second, db->channel->id, type);
    char *timestamp = time_str_get_date_time(image->time);
    char *keywords = image_keywords_as_hstore(image);
    char *sql = NULL;
    double ra0 = 0;
    double dec0 = 0;

    coords_get_ra_dec(&image->coords, 0.5*image->width, 0.5*image->height, &ra0, &dec0);

    sql = make_string("INSERT INTO images (channel_id, filename, night, time, type, ra0, dec0, width, height, filter, keywords) "
                      "VALUES (%d, '%s', '%s', '%s', '%s', %g, %g, %d, %d, get_filter_id('%s'), %s);",
                      db->channel->id, filename, night, timestamp, type, ra0, dec0,
                      image->width, image->height, filter, keywords);

    mkdir(db->channel->filepath, 0755);
    mkdir(dirname, 0755);
    image_dump_to_fits(image, filename);

    db_query(db->db, sql);

    free(sql);
    free(keywords);
    free(timestamp);
    free(filename);
    free(dirname);
    free(night);
}

void store_object_record(channel_db_str *db, int object_id, object_str *object, record_str *record)
{
    PGresult *res = NULL;
    char *time = time_str_get_date_time(record->time);
    char *hstore = make_string("sigma_x=>%g,sigma_y=>%g,excess=>%g", record->sigma_x, record->sigma_y, record->excess);

    res = db_query(db->db, "INSERT INTO realtime_records(object_id, time, ra, dec, x, y, a, b, theta, flux, flux_err, filter, mag, mag_err, flags, params) VALUES"
                   "(%d, '%s', %.10g, %.10g, %g, %g, %g, %g, %g, %g, %g, get_filter_id('%s'), %g, %g, %d, '%s');",
                   object_id, time, record->ra, record->dec, record->x, record->y, record->a, record->b, record->theta,
                   record->flux, record->flux_err, object->filter, record->mag, record->mag_err, record->flags, hstore);

    PQclear(res);

    free(time);
    free(hstore);
}

void store_object(channel_db_str *db, object_str *object)
{
    PGresult *res = NULL;
    time_str evening_time = time_incremented(object->time0, -12*60*60);
    char *night = make_string("%04d_%02d_%02d", evening_time.year, evening_time.month, evening_time.day);
    char *time_start = time_str_get_date_time(object->time0);
    char *time_end = time_str_get_date_time(object->time_last);
    char *state = object_state[object->state];
    char *params = object_params_as_hstore(object);
    double ra0 = 0;
    double dec0 = 0;

    dprintf("Storing object: id=%Ld\n", object->id);

    res = db_query(db->db, "INSERT INTO realtime_objects(channel_id, night, time_start, time_end, state, ra0, dec0, params) VALUES "
                   "(%d, '%s', '%s', '%s', get_realtime_object_state_id('%s'), %g, %g, %s) RETURNING id;",
                   db->channel->id, night, time_start, time_end, state, ra0, dec0, params);

    if(PQntuples(res)){
        int id = db_get_int(res, 0, 0);
        record_str *record = NULL;

        foreachback(record, object->records)
            store_object_record(db, id, object, record);
    }

    PQclear(res);

    free(night);
    free(time_start);
    free(time_end);
    free(params);
}

void store_secondscale_record(channel_db_str *db, record_extended_str *record)
{
    if(isfinite(record->flux) && record->flux > 0 &&
       isfinite(record->mag) && isfinite(record->mag_err)){
        PGresult *res = NULL;
        char *time = time_str_get_date_time(record->time);

        res = db_query(db->db, "INSERT INTO records (channel_id, time, filter, ra, dec, mag, mag_err, flux, flux_err, x, y, flags) VALUES "
                       "(%d, '%s', get_filter_id('%s'), %.10g, %.10g, %g, %g, %g, %g, %g, %g, %d) RETURNING id;",
                       db->channel->id, time, record->filter, record->ra, record->dec,
                       record->mag, record->mag_err, record->flux, record->flux_err, record->x, record->y, record->flags);

        PQclear(res);

        free(time);
    }
}

void *db_worker(void *data)
{
    channel_db_str *db = (channel_db_str *)malloc(sizeof(channel_db_str));
    channel_str *channel = (channel_str *)data;
    char *connstring = make_string("dbname=favor2 host=%s port=%d", config_db_host, config_db_port);
    int is_quit = FALSE;

    dprintf("DB writer started\n");

    db->channel = channel;

    db->db = db_create(connstring);

    while(!is_quit){
        queue_message_str m = queue_get(channel->db_queue);

        switch(m.event){
        case CHANNEL_MSG_SQL:
            /* SQL query */
            db_query(db->db, "%s", (char *)m.data);
            free(m.data);
            break;
        case CHANNEL_MSG_DB_FIND:
            /* Request to find latest file of specified type in DB */
            {
                char *filename = find_file_with_type(db, (char *)m.data);
                int type = -1;

                if(!strcmp((char *)m.data, "dark"))
                    type = CHANNEL_MSG_FILENAME_DARK;
                else //if(!strcmp((char *)m.data, "flat"))
                    type = CHANNEL_MSG_FILENAME_FLAT;

                if(filename)
                    queue_add_with_destructor(channel->server_queue, type, filename, free);
                else
                    queue_add(channel->server_queue, type, NULL);

                free(m.data);
                break;
            }
        case CHANNEL_MSG_IMAGE:
#ifndef ANDOR_FAKE
            store_image(db, (image_str *)m.data);
#endif
            image_delete((image_str *)m.data);
            break;
        case CHANNEL_MSG_OBJECT:
#ifndef ANDOR_FAKE
            store_object(db, (object_str *)m.data);
#endif
            object_delete((object_str *)m.data);
            break;
        case CHANNEL_MSG_SECONDSCALE_RECORD:
#ifndef ANDOR_FAKE
            store_secondscale_record(db, (record_extended_str *)m.data);
#endif
            record_extended_delete((record_extended_str *)m.data);
            break;
        case CHANNEL_MSG_EXIT:
            /* Quit request */
            is_quit = TRUE;
            break;
        }
    }

    db_delete(db->db);

    free(db);
    free(connstring);

    dprintf("DB writer finished\n");

    return NULL;
}
