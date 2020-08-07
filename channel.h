#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdarg.h>
#include <pthread.h>

#include "server.h"
#include "queue.h"
#include "script.h"
#include "image.h"
#include "command_queue.h"

#ifdef ANDOR
#include "andor.h"
#endif

typedef struct {
    /* Main network connector */
    server_str *server;

    /* Channel id */
    int id;

    /* Queue to main thread */
    queue_str *server_queue;

    /* Queue to grabber thread */
    queue_str *grabber_queue;

    /* Queue to realtime data processor */
    queue_str *realtime_queue;

    /* Queue to secondscale data processor */
    queue_str *secondscale_queue;

    /* Queue to image archiver */
    queue_str *archive_queue;

    /* Queue to raw data writer */
    queue_str *raw_queue;

    /* Queue to DB data writer */
    queue_str *db_queue;

    /* Dedicated hardware controller connection */
    connection_str *hw_connection;

    /* Dedicated beholder connection */
    connection_str *beholder_connection;

    /* Dedicated logger connection, accessible from all threads */
    connection_str *logger_connection;

    /* Mount connection */
    connection_str *mount_connection;

#ifdef ANDOR
    /* Andor */
    double andor_temperature;
    int andor_temperaturestatus;
    double andor_exposure;
    double andor_framerate;
    int andor_binning;
    int andor_filter;
    int andor_preamp;
    int andor_shutter;
#endif /* ANDOR */

    /* State variables */
    int realtime_state;
    int raw_state;
    int grabber_state;

    int is_focused;

    /* Global channel state */
    int state;

    /* Current hardware parameters */
    int hw_connected;
    int hw_focus;
    int hw_focus2;
    int hw_ii_power;
    int hw_cover;
    int hw_lamp;
    int hw_camera;
    char *hw_filter;
    int hw_mirror0;
    int hw_mirror1;

    double hw_camera_temperature;
    double hw_camera_humidity;
    double hw_camera_dewpoint;
    double hw_celostate_temperature;
    double hw_celostate_humidity;
    double hw_celostate_dewpoint;

    /* Current mount pointing */
    double ra0;
    double dec0;
    /* Relative pointing */
    double delta_ra;
    double delta_dec;
    /* Effective center of FOV */
    double ra;
    double dec;
    /* Coordinate transformation */
    coords_str coords;
    int image_width;
    int image_height;

    /* Image stats, updated occasionally */
    double image_mean;
    double image_std;
    double image_median;
    double image_mad;

    /* Processing state from subsystems */
    int realtime_nrecords;
    int realtime_nsingle;
    int realtime_ndouble;
    int realtime_nmeteor;
    int realtime_nmoving;
    int realtime_nflash;

    int secondscale_nobjects;
    double secondscale_mag0;
    double secondscale_mag_scale;
    double secondscale_mag_min;
    double secondscale_mag_max;
    double secondscale_mag_limit;

    /* Pixel scale */
    double pixscale;

    /* Target name */
    char *target_name;
    /* Target type */
    char *target_type;
    /* Target ID */
    int target_id;
    /* UUID of the target / observing plan */
    char *target_uuid;
    /* Target filter */
    char *target_filter;
    /* Target exposure */
    double target_exposure;
    double target_repeat;

    /* List of commands sent and awaiting reply */
    command_queue_str *commands;

    /* Control script */
    script_str *script;

    /* Pointer to current image */
    image_str *image;
    time_str last_image_time;

    /* Accumulator image */
    image_str *accum_image;
    int accum_length;
    double accum_exposure;

    /* Progress for various operations */
    double progress;

    /* Current dark frame and flatfield */
    image_str *dark;
    image_str *flat;

    /* Mask - map of noisy pixels */
    image_str *mask;

    /* Configuration parameters */
    char *filepath;
    char *rawpath;

    /* Celostate calibration */
    double mirror0_dra;
    double mirror0_ddec;
    double mirror1_dra;
    double mirror1_ddec;
    double mirror_dra;
    double mirror_ddec;

    /* Capabilities */
    int has_ii;

    int is_zoom;
    int scale;

    int is_quit;

    /* Mutex for occasional locking */
    pthread_mutex_t mutex;
} channel_str;

/* Subsystems */
void *grabber_worker(void *);
void *realtime_worker(void *);
void *secondscale_worker(void *);
void *archive_worker(void *);
void *raw_worker(void *);
void *db_worker(void *);

/* Central commands processor */
void process_command(server_str *, connection_str *, char *, void *);

/* Logger */
void channel_log(channel_str *, const char *, ...)  __attribute__ ((format (printf, 2, 3)));

/* Image storage, both on disk and in DB */
void channel_store_image(channel_str *, image_str *, char *);

/* Status string */
char *channel_status_string(channel_str *);

/* Celostate calibration */
void channel_calibrate_celostates(channel_str *, char *, char *);

/* Messages to pass over the bus */
#define CHANNEL_MSG_IMAGE 11
#define CHANNEL_MSG_COORDS 12
#define CHANNEL_MSG_OBJECT 13
#define CHANNEL_MSG_ALERT 14
#define CHANNEL_MSG_SECONDSCALE_RECORD 15
#define CHANNEL_MSG_SQL 16
#define CHANNEL_MSG_PROCESSED_IMAGE 17

#define CHANNEL_MSG_RESET 20
#define CHANNEL_MSG_START 21
#define CHANNEL_MSG_STOP 22

#define CHANNEL_MSG_DB_FIND 100
#define CHANNEL_MSG_FILENAME_DARK 101
#define CHANNEL_MSG_FILENAME_FLAT 102
#define CHANNEL_MSG_IMAGE_DARK 103
#define CHANNEL_MSG_IMAGE_FLAT 104

#define CHANNEL_MSG_FRAME_INFO 110

#define CHANNEL_MSG_PROCESSING_START 200
#define CHANNEL_MSG_PROCESSING_STARTED 201
#define CHANNEL_MSG_PROCESSING_STOP 202
#define CHANNEL_MSG_PROCESSING_STOPPED 203

#define CHANNEL_MSG_STORAGE_START 204
#define CHANNEL_MSG_STORAGE_STARTED 205
#define CHANNEL_MSG_STORAGE_STOP 206
#define CHANNEL_MSG_STORAGE_STOPPED 207

#define CHANNEL_MSG_GRABBER_START 208
#define CHANNEL_MSG_GRABBER_STARTED 209
#define CHANNEL_MSG_GRABBER_STOP 210
#define CHANNEL_MSG_GRABBER_STOPPED 211

#define CHANNEL_MSG_COMMAND 300
#define CHANNEL_MSG_COMMAND_DONE 301

#define CHANNEL_MSG_EXIT 1000

typedef struct {
    int width;
    int height;
    coords_str coords;
    time_str time;
    time_str midtime;
    double exposure;
} frame_info_str;

/* Channel state */
#define CHANNEL_STATE_NORMAL 0
#define CHANNEL_STATE_WRITE_DARKS 1
#define CHANNEL_STATE_AUTOFOCUS 2
#define CHANNEL_STATE_MONITORING 3
#define CHANNEL_STATE_FOLLOWUP 4
#define CHANNEL_STATE_WRITE_FLATS 5
#define CHANNEL_STATE_LOCATE 6
#define CHANNEL_STATE_RESET 7
#define CHANNEL_STATE_CALIBRATE 8
#define CHANNEL_STATE_IMAGING 9
#define CHANNEL_STATE_ERROR 100

/* Subsystem states */
#define STATE_OFF 0
#define STATE_ON 1
#define STATE_STARTING 2
#define STATE_ERROR -1

/* Global variables to pass some initial parameters between subsystems */
extern int config_grabber_udp_port;
extern char *config_grabber_host;
extern int config_grabber_port;
extern char *config_db_host;
extern int config_db_port;

#endif /* CHANNEL_H */
