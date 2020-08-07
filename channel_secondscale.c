#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "channel.h"
#include "image.h"
#include "field.h"
#include "sextractor.h"
#include "match.h"

static __thread struct list_head stars;
static __thread double prev_ra = -1;
static __thread double prev_dec = -1;
static __thread double prev_sr = 0;

static void process_frame(channel_str *channel, image_str *image, image_str *dark, image_str *flat)
{
    image_str *processed = image_create_double(image->width, image->height);;
    field_str *field = field_create();
    struct list_head objects;
    int d;

    if(image->type == IMAGE_UINT16){
        if(dark && flat){
            for(d = 0; d < image->width*image->height; d++){
                processed->double_data[d] = image->data[d] - dark->double_data[d];
                if(isfinite(flat->double_data[d]) && flat->double_data[d] > 0)
                    processed->double_data[d] /= flat->double_data[d];
            }
        } else if(dark)
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->data[d] - dark->double_data[d];
        else
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->data[d];
    } else {
        if(dark && flat){
            for(d = 0; d < image->width*image->height; d++){
                processed->double_data[d] = image->double_data[d] - dark->double_data[d];
                if(isfinite(flat->double_data[d]) && flat->double_data[d] > 0)
                    processed->double_data[d] /= flat->double_data[d];
            }
        } else if(dark)
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->double_data[d] - dark->double_data[d];
        else
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->double_data[d];
    }

    /* sextractor_get_stars_list(image, 100000, &objects); */
    sextractor_get_stars_list_aper(processed, 100000, &objects, 10.0);

    pthread_mutex_lock(&channel->mutex);
    channel->secondscale_nobjects = list_length(&objects);
    pthread_mutex_unlock(&channel->mutex);

    /* Do we have enough stars for calibration? */
    if(list_length(&objects) > 10){
        /* We do not believe channel-provided coords, let's perform blind match */
        field->coords = blind_match_coords_from_list(&objects, image->width, image->height);

        if(!coords_is_empty(&field->coords)){
            double ra0 = 0;
            double dec0 = 0;

            /* 3 pixels as a guess of coordinate uncertainty */
            field->max_dist = 3.0*coords_get_pixscale(&field->coords);

            /* Get more accurate center of frame */
            coords_get_ra_dec(&field->coords, 0.5*image->width, 0.5*image->height, &ra0, &dec0);

            if(prev_ra < 0 || prev_sr <= 0 || coords_sky_distance(prev_ra, prev_dec, ra0, dec0) > 0.1*prev_sr){
                /* We have to get new stars as coordinates are changed too much */
                free_list(stars);

                /* By default we will use WBVR catalogue */
                pickles_get_stars_list(ra0, dec0, 0.6*coords_distance(&field->coords, 0, 0, image->width, image->height), 100000, &stars);
                prev_ra = ra0;
                prev_dec = dec0;
                prev_sr = 0.6*coords_distance(&field->coords, 0, 0, image->width, image->height);
            }

            /* Refine WCS coordinates using these stars */
            coords_refine_from_list(&field->coords, &objects, &stars, 1.0, image->width, image->height);

            /* Guess the filter from FITS keywords */
            field_set_filter(field, image_keyword_get_string(image, "FILTER"));

            field_set_stars(field, &stars, "Pickles");

            if(list_length(&stars) > 10){
                field_calibrate(field, &objects);

                /* Remember coordinates, calibrations etc */
                field_annotate_image(field, image);
            } else
                /* At least set coordinates to the image */
                image->coords = field->coords;

            /* Send frame info to server sub-system and to realtime */
            {
                frame_info_str *info = (frame_info_str *)malloc(sizeof(frame_info_str));
                frame_info_str *info2 = (frame_info_str *)malloc(sizeof(frame_info_str));

                info->width = image->width;
                info->height = image->height;
                info->coords = image->coords;
                info->time = image->time;
                info->exposure =  image_keyword_get_double(image, "TOTAL EXPOSURE");
                info->midtime =  image_keyword_get_time(image, "MIDTIME");

                memcpy(info2, info, sizeof(frame_info_str));

                queue_add_with_destructor(channel->server_queue, CHANNEL_MSG_FRAME_INFO, info, free);
                queue_add_with_destructor(channel->realtime_queue, CHANNEL_MSG_FRAME_INFO, info2, free);
            }

            /* Coords will be freed in corresponding subsystem */
            /* { */
            /*     coords_str *coords = (coords_str *)malloc(sizeof(coords_str)); */

            /*     *coords = field->coords; */
            /*     queue_add_with_destructor(channel->realtime_queue, CHANNEL_MSG_COORDS, coords, free); */
            /* } */

            /* Send all detected objects to DB */
            {
                record_str *object = NULL;

                foreach(object, objects){
                    /* We will store only 'good' objects */
                    if(!(object->flags & RECORD_BAD) &&
                       !(object->flags & RECORD_TRUNCATED) &&
                       !(object->flags & RECORD_SATURATED) &&
                       !(object->flags & RECORD_ELONGATED)){
                        record_extended_str *copy = realloc(record_copy(object), sizeof(record_extended_str));

                        copy->time = image->time;
                        copy->filter = make_string(image_keyword_get_string(image, "FILTER"));

                        coords_get_ra_dec(&field->coords, copy->x, copy->y, &copy->ra, &copy->dec);

                        copy->mag = coords_get_mag(&field->coords, -2.5*log10(copy->flux), copy->x, copy->y, &copy->mag_err);
                        copy->mag_err = hypot(copy->mag_err, 2.5*log10(1 + copy->flux_err/copy->flux));

                        /* queue_add_with_destructor(channel->db_queue, CHANNEL_MSG_SECONDSCALE_RECORD, copy, (void (*)(void *))record_extended_delete); */
                        record_extended_delete(copy);
                    }
                }
            }
        }
    }

    pthread_mutex_lock(&channel->mutex);

    channel->secondscale_mag0 = 0;//field->coords.mag0;
    channel->secondscale_mag_scale = 1;//field->coords.mag_scale;
    channel->secondscale_mag_min = field->mag_min;
    channel->secondscale_mag_max = field->mag_max;
    channel->secondscale_mag_limit = field->mag_limit;

    pthread_mutex_unlock(&channel->mutex);

    dprintf("Second scale image processed\n");

    /* Write the image with matched coords to archive */
    channel_store_image(channel, image, "avg");

    /* The image ends its lifetime here */
    image_delete(image);

    free_list(objects);

    field_delete(field);
    image_delete(processed);
}

void process_frame_to_db(channel_str *channel, image_str *image, image_str *dark, image_str *flat)
{
    image_str *processed = image_create_double(image->width, image->height);;
    struct list_head objects;
    int d;

    if(image->type == IMAGE_UINT16){
        if(dark && flat)
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = (image->data[d] - dark->double_data[d])/flat->double_data[d];
        else if(dark)
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->data[d] - dark->double_data[d];
        else
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->data[d];
    } else {
        if(dark && flat)
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = (image->double_data[d] - dark->double_data[d])/flat->double_data[d];
        else if(dark)
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->double_data[d] - dark->double_data[d];
        else
            for(d = 0; d < image->width*image->height; d++)
                processed->double_data[d] = image->double_data[d];
    }

    //image->coords = blind_match_coords(processed);
    sextractor_get_stars_list_aper(processed, 100000, &objects, 10.0);

    if(list_length(&objects) > 10){
        double ra0;
        double dec0;

        //image->coords = blind_match_coords_from_list_and_refine(&objects, 1.0, image->width, image->height);
        image->coords = blind_match_coords_from_list(&objects, image->width, image->height);

        coords_get_ra_dec(&image->coords, 0.5*image->width, 0.5*image->height, &ra0, &dec0);

        if(prev_ra < 0 || prev_sr <= 0 || coords_sky_distance(prev_ra, prev_dec, ra0, dec0) > 0.1*prev_sr){
            free_list(stars);
            pickles_get_stars_list(ra0, dec0, 0.6*coords_distance(&image->coords, 0, 0, image->width, image->height), 100000, &stars);
            prev_ra = ra0;
            prev_dec = dec0;
            prev_sr = 0.6*coords_distance(&image->coords, 0, 0, image->width, image->height);
        }

        coords_refine_from_list(&image->coords, &objects, &stars, 1.0, image->width, image->height);
    }

    free_list(objects);

    queue_add_with_destructor(channel->db_queue, CHANNEL_MSG_IMAGE, image, (void (*)(void *))image_delete);

    image_delete(processed);
}

void *secondscale_worker(void *data)
{
    channel_str *channel = (channel_str *)data;
    image_str *dark = NULL;
    image_str *flat = NULL;
    int is_quit = FALSE;

    dprintf("Secondscale started\n");

    init_list(stars);

    while(!is_quit){
        queue_message_str m = queue_get(channel->secondscale_queue);

        switch(m.event){
        case CHANNEL_MSG_IMAGE:
            /* Image to process */
            process_frame(channel, (image_str *)m.data, dark, flat);
            break;
        case CHANNEL_MSG_EXIT:
            /* Quit request */
            is_quit = TRUE;
            break;
        case CHANNEL_MSG_IMAGE_DARK:
            if(dark)
                image_delete_and_null(dark);
            dark = (image_str *)m.data;
            break;
        case CHANNEL_MSG_IMAGE_FLAT:
            if(flat)
                image_delete_and_null(flat);
            flat = (image_str *)m.data;
            break;
        default:
            break;
        }
    }

    if(dark)
        image_delete(dark);
    if(flat)
        image_delete(flat);

    dprintf("Secondscale finished\n");

    return NULL;
}

void *archive_worker(void *data)
{
    channel_str *channel = (channel_str *)data;
    image_str *dark = NULL;
    image_str *flat = NULL;
    int is_quit = FALSE;

    dprintf("Archive started\n");

    init_list(stars);

    while(!is_quit){
        queue_message_str m = queue_get(channel->archive_queue);

        switch(m.event){
        case CHANNEL_MSG_IMAGE:
            /* Image to match coords */
            process_frame_to_db(channel, (image_str *)m.data, dark, flat);
            break;
        case CHANNEL_MSG_EXIT:
            /* Quit request */
            is_quit = TRUE;
            break;
        case CHANNEL_MSG_IMAGE_DARK:
            if(dark)
                image_delete_and_null(dark);
            dark = (image_str *)m.data;
            break;
        case CHANNEL_MSG_IMAGE_FLAT:
            if(flat)
                image_delete_and_null(flat);
            flat = (image_str *)m.data;
            break;
        default:
            break;
        }
    }

    if(dark)
        image_delete(dark);
    if(flat)
        image_delete(flat);

    dprintf("Archive finished\n");

    return NULL;
}
