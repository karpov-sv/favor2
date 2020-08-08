#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>

#include "utils.h"

#include "channel.h"
#include "time_str.h"
#include "image_binary.h"

#define MIN_FREE_SPACE 250ULL*1024*1024*1024 /* Minimal free space we will try to keep */

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

/* Maximal time interval to reset the file. In seconds. */
#define MAX_TIME_INTERVAL 10
/* Maximal number of files in FITS dir */
#define MAX_FITS_NUMBER 1000

typedef struct {
    char *base;
    char *dir;
    FILE *idx;
    int nfiles;
    time_str last_time;
    double last_ra;
    double last_dec;
    double last_exposure;
} fits_writer_str;

/* FITS writer */
fits_writer_str *fits_writer_create(const char *base)
{
    fits_writer_str *fits = (fits_writer_str *)malloc(sizeof(fits_writer_str));

    /* Create the directory if it does not exist already */
    mkdir(base, 0755);

    /* TODO: check the writability of the base directory */

    fits->base = make_string("%s", base);
    fits->dir = NULL;
    fits->idx = NULL;

    fits->last_time = time_zero();
    fits->last_ra = 0;
    fits->last_dec = 0;
    fits->last_exposure = 0;

    fits->nfiles = 0;

    return fits;
}

void fits_writer_delete(fits_writer_str *fits)
{
    if(!fits)
        return;

    if(fits->base)
        free(fits->base);
    if(fits->dir)
        free(fits->dir);
    if(fits->idx)
        fclose(fits->idx);

    free(fits);
}

/* Check the free space in given dir and delete older data dirs if
   necessary */
void fits_writer_cleanup(fits_writer_str *fits)
{
    struct dirent **entry = NULL;
    int length = scandir((const char *)fits->base, &entry,
                         lambda(int, (const struct dirent *entry),
                                {
                                    if(entry->d_type == DT_DIR &&
                                       fnmatch("????????_??????", entry->d_name, 0) == 0)
                                        return 1;
                                    else
                                        return 0;
                                }), alphasort);
    int deleted = 0;
    int d;

    dprintf("%d dirs\n", length);

    while(free_disk_space(fits->base) < MIN_FREE_SPACE && length - deleted > 2){
        char *dirname = make_string("%s/%s", fits->base, entry[deleted]->d_name);

        dprintf("Deleting %s\n", dirname);
        remove_dir(dirname);

        free(dirname);

        deleted ++;
    }

    /* Cleanup the results of scandir() */
    if(length){
        for(d = 0; d < length; d++)
            free(entry[d]);

        free(entry);
    }
}

void fits_writer_new_dir(fits_writer_str *fits, time_str time)
{
    char *idxname = NULL;

    if(fits->dir)
        free(fits->dir);

    if(fits->idx)
        fclose(fits->idx);

    /* FIXME: should we include fractions of second here? */
    fits->dir = make_string("%s/%04d%02d%02d_%02d%02d%02d", fits->base,
                            time.year, time.month, time.day, time.hour, time.minute, time.second);
    mkdir(fits->dir, 0755);

    dprintf("New dir created: %s\n", fits->dir);

    idxname = make_string("%s/0index.txt", fits->dir);
    fits->idx = fopen(idxname, "w");
    free(idxname);

    /* Dedicated cleaner thread will monitor the free space */
    /* fits_writer_cleanup(fits); */

    fits->nfiles = 0;
    fits->last_time = time;
}

void fits_writer_write(fits_writer_str *fits, image_str *image)
{
    /* TODO: should we just use image->framenumber here? */
    u_int64_t framenumber = time_str_get_uuid(image->time);
    char *filename = NULL;
    double ra = image->coords.ra0;
    double dec = image->coords.dec0;
    int wcs = !coords_is_empty(&image->coords);

    if(wcs)
        /* Get exact coordinates of field center, if available */
        coords_get_ra_dec(&image->coords, 0.5*image->width, 0.5*image->height, &ra, &dec);

#ifdef PROFILE_PERFORMANCE
    timer_t1 = current_time();
    printf("raw %g\n", timer_t1 - timer_t0);
    timer_t0 = timer_t1;
#endif /* PROFILE_PERFORMANCE */

    if(!fits->dir || fits->nfiles >= MAX_FITS_NUMBER ||
       time_interval(fits->last_time, image->time) >= 1e3*MAX(MAX_TIME_INTERVAL, 2.0*image_keyword_get_double(image, "EXPOSURE")) ||
       fits->last_exposure != image_keyword_get_double(image, "EXPOSURE") ||
       coords_sky_distance(ra, dec, fits->last_ra, fits->last_dec) > 0.5)
        fits_writer_new_dir(fits, image->time);

    /* filename = make_string("%s/%lld.fits", fits->dir, framenumber); */
    filename = make_string("%s/%lld.fits[compress]", fits->dir, framenumber);
    image_dump_to_fits(image, filename);
    fits->nfiles ++;
    fits->last_time = image->time;
    fits->last_ra = ra;
    fits->last_dec = dec;
    fits->last_exposure = image_keyword_get_double(image, "EXPOSURE");

    /* */
    if(fits->idx){
        fprintf(fits->idx, "%lld.fits %s %s %s %s %d %d %d %s %g %g\n",
                framenumber, image_keyword_get_string(image, "FILTER"),
                image_keyword_get_int(image, "COVER") ? "Open" : "Closed",
                image_keyword_get_int(image, "LAMP") ? "Lamp" : "NoLamp",
                image_keyword_get_int(image, "II_POWER") ? "II" : "NoII",
                image_keyword_get_int(image, "FOCUS"),
                image_keyword_get_int(image, "MIRROR_POS0"), image_keyword_get_int(image, "MIRROR_POS1"),
                wcs ? "WCS" : "NoWCS",
                ra, dec);

        fflush(fits->idx);
    }

    free(filename);
}

/* Cleaner worker */
void *cleaner_worker(void *data)
{
    fits_writer_str *fits = (fits_writer_str *)data;

    dprintf("Disk cleaner started\n");

    while(TRUE){
        fits_writer_cleanup(fits);
        sleep(120);
    }
}

/* Thread worker */
void *raw_worker(void *data)
{
    channel_str *channel = (channel_str *)data;
    fits_writer_str *fits = fits_writer_create(channel->rawpath);
    pthread_t cleaner_thread;
    int is_quit = FALSE;

    /* First - free some disk space */
    fits_writer_cleanup(fits);
    /* ..and run the cleaner thread */
    pthread_create(&cleaner_thread, NULL, cleaner_worker, (void*)fits);

    dprintf("Raw writer started and is ready to write the data to %s\n", channel->rawpath);

    while(!is_quit){
        queue_message_str m = queue_get(channel->raw_queue);

        switch(m.event){
        case(CHANNEL_MSG_IMAGE):
            /* Image to store */
            if(channel->raw_state == STATE_ON)
                fits_writer_write(fits, (image_str *)m.data);
            image_delete((image_str *)m.data);
            break;
        case(CHANNEL_MSG_EXIT):
            /* Quit request */
            is_quit = TRUE;
            break;
        case CHANNEL_MSG_STORAGE_START:
            if(channel->raw_state != STATE_ON)
                channel_log(channel, "Storage started");

            pthread_mutex_lock(&channel->mutex);
            channel->raw_state = STATE_ON;
            pthread_mutex_unlock(&channel->mutex);

            queue_add(channel->server_queue, CHANNEL_MSG_STORAGE_STARTED, NULL);
            break;
        case CHANNEL_MSG_STORAGE_STOP:
            if(channel->raw_state != STATE_OFF)
                channel_log(channel, "Storage stopped");

            pthread_mutex_lock(&channel->mutex);
            channel->raw_state = STATE_OFF;
            pthread_mutex_unlock(&channel->mutex);

            queue_add(channel->server_queue, CHANNEL_MSG_STORAGE_STOPPED, NULL);
            break;
        default:
            break;
        }
    }

    pthread_cancel(cleaner_thread);
    pthread_join(cleaner_thread, NULL);

    dprintf("Raw writer finished\n");

    fits_writer_delete(fits);

    return NULL;
}
