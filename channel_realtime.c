#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "utils.h"

#include "channel.h"
#include "realtime.h"
#include "realtime_types.h"
#include "stars.h"

#ifdef PROFILE_PERFORMANCE
#include <sys/time.h>
static double timer_t0 = 0;
static double timer_t1 = 0;

static inline double current_time(void) {
   struct timeval t;
   gettimeofday(&t,NULL);
   return (double) t.tv_sec + t.tv_usec/1000000.0;
}
#endif /* PROFILE_PERFORMANCE */

void dump_records(realtime_str *realtime, void *data)
{
    channel_str *channel = (channel_str *)data;
    int nnoise = 0;

    record_str *record;

    foreach(record, realtime->records)
        if(record->flags & RECORD_NOISE)
            nnoise ++;

    pthread_mutex_lock(&channel->mutex);

    channel->realtime_nrecords = list_length(&realtime->records);

    pthread_mutex_unlock(&channel->mutex);

    //dprintf("%d records, %d noise\n", channel->realtime_nrecords, nnoise);
}

void dump_objects(realtime_str *realtime, void *data)
{
    channel_str *channel = (channel_str *)data;
    object_str *object;
    char *timestamp = time_str_get_date_time(realtime->time);
    int number = 0;
    int number_single = 0;
    int number_double = 0;
    int number_checking = 0;
    int number_moving = 0;
    int number_flashes = 0;
    int number_meteor = 0;

    foreach(object, realtime->objects){
        number ++;

        if(object->state == OBJECT_SINGLE)
            number_single ++;
        if(object->state == OBJECT_DOUBLE)
            number_double ++;
        if(object->state == OBJECT_CHECKING)
            number_checking ++;
        if(object->state == OBJECT_MOVING)
            number_moving ++;
        if(object->state == OBJECT_FLASH)
            number_flashes ++;
        if(object->state == OBJECT_METEOR)
            number_meteor ++;
    }

    dprintf("%d objects (S: %d, D: %d, C: %d, m: %d, M: %d, F:%d) at %s\n",
            number, number_single, number_double, number_checking, number_moving, number_meteor, number_flashes, timestamp);

    pthread_mutex_lock(&channel->mutex);

    channel->realtime_nsingle = number_single;
    channel->realtime_ndouble = number_double;
    channel->realtime_nmeteor = number_meteor;
    channel->realtime_nmoving = number_moving;
    channel->realtime_nflash = number_flashes;

    pthread_mutex_unlock(&channel->mutex);

    free(timestamp);
}

static void classify_object(channel_str *channel, object_str *object)
{
    char *result = NULL;

    if(object->state == OBJECT_FLASH && !coords_is_empty(&object->coords)){
        struct list_head stars;
        double ra = 0;
        double dec = 0;
        double pixscale = coords_get_pixscale(&object->coords);
        star_str *star = NULL;

        coords_get_ra_dec(&object->coords, object->Cx[0], object->Cy[0], &ra, &dec);

        init_list(stars);

        tycho2_get_stars_list(ra, dec, 5.0*pixscale, 1, &stars);

        foreach(star, stars)
            if(coords_get_mag(&object->coords, object_max_flux(object), object->Cx[0], object->Cy[0], NULL) > star->v - 1){
                /* Star is bright enough to provide the flash we see */
                if(result)
                    add_to_string(&result, ", ");
                add_to_string(&result, "%g mag star at %g arcsec", star->v, coords_sky_distance(ra, dec, star->ra, star->dec)*3600);
            }

        free_list(stars);
    }

    free_and_null(object->classification);

    object->classification = result;
}

double get_object_ang_length(object_str *object)
{
    record_str *record1 = list_first_item(object->records);
    record_str *record2 = list_last_item(object->records);

    return coords_sky_distance(record1->ra, record1->dec, record2->ra, record2->dec);
}

static void store_object(realtime_str *realtime, object_str *object, void *data)
{
    channel_str *channel = (channel_str *)data;

    dprintf("Object %Ld finished\n", object->id);
    dprintf("state=%d length=%d\n", object->state, object->length);

    /* Probably the coordinates were not matched when object was created */
    if(coords_is_empty(&object->coords))
        object->coords = realtime->coords;

    /* Filter (FIXME: most probably!..) used to observe the object */
    free_and_null(object->filter);
    object->filter = make_string(channel->hw_filter);

    /* classify_object(channel, object); */

    if(object->state == OBJECT_MOVING && !coords_is_empty(&object->coords) && get_object_ang_length(object) < 0.1){
        dprintf("Object %Ld has too short track (%g deg), deleting it\n", object->id, get_object_ang_length(object));

        return;
    }

    /* Refine object coordinates - for moving only, for now */
    if(object->state == OBJECT_MOVING)
        realtime_fix_object_coords(realtime, object);

    /* Send a copy to DB subsystem for permanent storage */
    /* FIXME: how to avoid object duplication here?.. */
    queue_add_with_destructor(channel->db_queue, CHANNEL_MSG_OBJECT, object_copy(object), (void (*)(void *))object_delete);
}

static void report_object(realtime_str *realtime, object_str *object, void *data)
{
    channel_str *channel = (channel_str *)data;

    dprintf("Object %Ld should be reported\n", object->id);
    dprintf("state=%d length=%d\n", object->state, object->length);

    /* Probably the coordinates were not matched when object was created */
    if(coords_is_empty(&object->coords))
        object->coords = realtime->coords;

    /* Filter (FIXME: most probably!..) used to observe the object */
    free_and_null(object->filter);
    object->filter = make_string(channel->hw_filter);

    classify_object(channel, object);

    /* Send a copy to main thread for reporting */
    /* FIXME: how to avoid object duplication here?.. */
    queue_add_with_destructor(channel->server_queue, CHANNEL_MSG_OBJECT, object_copy(object), (void (*)(void *))object_delete);
}

static void process_frame(channel_str *channel, realtime_str *realtime, image_str *image)
{
#ifdef PROFILE_PERFORMANCE
    timer_t1 = current_time();
    printf("realtime %g\n", timer_t1 - timer_t0);
    timer_t0 = timer_t1;
#endif /* PROFILE_PERFORMANCE */

#ifdef ANDOR
    if(realtime->saturation_level <= 0){
        /* Guess saturation level from SHUTTER keyword */
        if(image_keyword_get_int(image, "SHUTTER") == 0)
            realtime->saturation_level = 40000;
        else
            realtime->saturation_level = 10000;
    }
#endif

    realtime_process_frame(realtime, image);

    if(average_image_is_full(realtime->avg)){
        image_str *avg_image = average_image_mean(realtime->avg);

        queue_add_with_destructor(channel->secondscale_queue, CHANNEL_MSG_IMAGE, avg_image, (void (*)(void *))image_delete);

        average_image_reset_average(realtime->avg);
    }

    if(!coords_is_empty(&realtime->coords))
        image->coords = realtime->coords;

    /* We will not need to delete the frame here - it will be sent back to the main thread */
    /* image_delete(image); */
}

void *realtime_worker(void *data)
{
    channel_str *channel = (channel_str *)data;
    realtime_str *realtime = realtime_create(100);
    int is_quit = FALSE;

    REALTIME_SET_HOOK(realtime, records_extracted_hook, dump_records, channel);
    REALTIME_SET_HOOK(realtime, objects_iterated_hook, dump_objects, channel);
    REALTIME_SET_HOOK(realtime, object_finished_hook, store_object, channel);
    REALTIME_SET_HOOK(realtime, flash_detected_hook, report_object, channel);

    /* Bad pixel map */
    realtime->badpix = channel->mask;

    /* Empirical estimate based on satellite tracks */
    realtime->sigma_correction = 1.2;

    dprintf("Realtime started\n");

    while(!is_quit){
        queue_message_str m = queue_get(channel->realtime_queue);

        /* Reset the realtime if queue is too long */
        if(queue_length(channel->realtime_queue) > 50)
            realtime_reset(realtime);

        switch(m.event){
        case CHANNEL_MSG_IMAGE:
            /* Image to process */
            if(channel->realtime_state == STATE_ON)
                process_frame(channel, realtime, (image_str *)m.data);
            /* Send it to the storage */
            queue_add_with_destructor(channel->raw_queue, CHANNEL_MSG_IMAGE, m.data, (void (*)(void *))image_delete);
            break;
        case CHANNEL_MSG_COORDS:
            /* WCS info from secondscale */
            realtime->coords = *(coords_str *)m.data;
            free(m.data);
            break;
        case CHANNEL_MSG_FRAME_INFO:
            if(m.data){
                frame_info_str *info = (frame_info_str *)m.data;

                realtime_add_coords(realtime, info->coords, info->midtime);
                free(m.data);
            }
            break;
        case CHANNEL_MSG_RESET:
            /* We should reset the realtime processing */
            realtime_reset(realtime);
            break;
        case CHANNEL_MSG_EXIT:
            /* Quit request */
            is_quit = TRUE;
            break;
        case CHANNEL_MSG_PROCESSING_START:
            if(channel->realtime_state != STATE_ON)
                channel_log(channel, "Processing started");

            pthread_mutex_lock(&channel->mutex);

            channel->realtime_state = STATE_ON;

#ifdef ANDOR
            /* Reset the saturation level so it will be guessed on first frame */
            realtime->saturation_level = 0;
#endif

            pthread_mutex_unlock(&channel->mutex);

            realtime_reset(realtime);
            queue_add(channel->server_queue, CHANNEL_MSG_PROCESSING_STARTED, NULL);
            break;
        case CHANNEL_MSG_PROCESSING_STOP:
            if(channel->realtime_state != STATE_OFF)
                channel_log(channel, "Processing stopped");

            pthread_mutex_lock(&channel->mutex);

            channel->realtime_state = STATE_OFF;

            channel->realtime_nrecords = 0;
            channel->realtime_nsingle = 0;
            channel->realtime_ndouble = 0;
            channel->realtime_nmeteor = 0;
            channel->realtime_nmoving = 0;
            channel->realtime_nflash = 0;

            pthread_mutex_unlock(&channel->mutex);

            realtime_reset(realtime);
            queue_add(channel->server_queue, CHANNEL_MSG_PROCESSING_STOPPED, NULL);
            break;

        case CHANNEL_MSG_IMAGE_DARK:
            if(realtime->dark)
                image_delete_and_null(realtime->dark);
            realtime->dark = (image_str *)m.data;

            break;

        case CHANNEL_MSG_IMAGE_FLAT:
            if(realtime->flat)
                image_delete_and_null(realtime->flat);
            realtime->flat = (image_str *)m.data;

            break;

        default:
            break;
        }
    }

    dprintf("Realtime finished\n");

    realtime_delete(realtime);

    return NULL;
}
