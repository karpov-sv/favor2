#ifndef REALTIME_H
#define REALTIME_H

#include "image.h"
#include "average.h"

/* Forward declaration */
struct object_str;

#define REALTIME_DECLARE_HOOK(name, ...) \
    void (*name)(struct realtime_str *, ## __VA_ARGS__, void *); \
    void *name ## _data
#define REALTIME_CALL_HOOK(rt, name, ...) \
    if((rt)->name) (rt)->name((rt), ## __VA_ARGS__, (rt)->name ## _data)
#define REALTIME_SET_HOOK(rt, name, func, data) \
    do {(rt)->name = (func); \
        (rt)->name ## _data = (data);} while(0)
/* #define REALTIME_SET_HOOK(rt, name, func) \ */
/*     do {(rt)->name = (func); \ */
/*         (rt)->name ## _data = NULL;} while(0) */
/* #define REALTIME_SET_HOOK(rt, name, func) \ */
/*     REALTIME_SET_HOOK((rt), name, (func), NULL) */

typedef struct realtime_str {
    /* Average frame */
    average_image_str *avg;
    /* Current coordinate transformation */
    coords_str coords;
    /* List of currently tracked transients */
    struct list_head objects;

    /* List of records on the current frame */
    struct list_head records;
    /* Current time */
    time_str time;

    /* Running mean number of records */
    double mean_num_records;

    /* Object id counter */
    u_int64_t object_counter;

    /* Tunable configuration parameters */
    int saturation_level;
    double initial_catch_radius;
    double max_meteor_angle;
    double meteor_elongation_threshold;
    double meteor_length_threshold;
    double flash_reporting_interval;
    double flash_invisibility_interval;
    double moving_invisibility_interval;
    double sigma_correction;

    int max_records_per_frame;

    double gain; /* Electrons per ADU */

    /* Map of bad pixels */
    image_str *badpix;

    /* Dark */
    image_str *dark;

    /* Flat */
    image_str *flat;

    /* Whether the system should work with no coordinate transform provided */
    int should_work_without_coords;

    /* Historical coordinate transformations */
    coords_str *hcoords;
    time_str *htime;
    int hlength;

    /* User-supplied hooks */
    REALTIME_DECLARE_HOOK(records_extracted_hook);
    REALTIME_DECLARE_HOOK(objects_iterated_hook);
    REALTIME_DECLARE_HOOK(object_finished_hook, struct object_str *);
    REALTIME_DECLARE_HOOK(flash_detected_hook, struct object_str *);
    REALTIME_DECLARE_HOOK(flash_cancelled_hook, struct object_str *);
} realtime_str;

realtime_str *realtime_create(int );
void realtime_delete(realtime_str *);

void realtime_process_frame(realtime_str *, image_str *);

void realtime_reset(realtime_str *);

void realtime_add_coords(realtime_str *, coords_str , time_str );
void realtime_fix_object_coords(realtime_str *, struct object_str *);

/* For internal use */
void realtime_find_records(realtime_str *, image_str *);
void realtime_classify_records(realtime_str *);
void realtime_cleanup_objects(realtime_str *);
void realtime_adjust_parameters(realtime_str *);

/* Temporary function */
double get_median_diff(realtime_str *, image_str *, image_str *, int , int );

#endif /* REALTIME_H */
