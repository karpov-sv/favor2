#ifndef REALTIME_TYPES_H
#define REALTIME_TYPES_H

#include "lists.h"
#include "time_str.h"
#include "records.h"

/* Single image pixel */
typedef struct pixel_str {
    LIST_HEAD(struct pixel_str);

    int x;
    int y;
    double excess;
    double flux;
    double flux_err;
} pixel_str;

/* Region on an image */
typedef struct region_str {
    LIST_HEAD(struct region_str);

    struct region_str *replaced;

    int npixels;
    int npixels_significant;

    int is_edge;
    int is_noise;

    record_str *record;

    struct list_head pixels;
} region_str;

#define OBJECT_MAXPARAMS 6

/* Readable representation of object types */
static char *object_state[]  __attribute__ ((unused)) = {"meteor", "single", "double", "moving", "flash", "checking"};

/* Group of regions from different frames */
typedef struct object_str {
    LIST_HEAD(struct object_str);

    /* Object id */
    u_int64_t id;

    /* Coordinates, if any */
    coords_str coords;

    /* Current filter */
    char *filter;

    /* Trajectory */
    time_str time0;
    time_str time_last;
    double Cx[OBJECT_MAXPARAMS];
    double Cy[OBJECT_MAXPARAMS];

    /* Object state */
    enum object_state {
        OBJECT_METEOR = 0,
        OBJECT_SINGLE = 1,
        OBJECT_DOUBLE = 2,
        OBJECT_MOVING = 3,
        OBJECT_FLASH = 4,
        OBJECT_CHECKING = 5,
        OBJECT_FINISHED = 255
    } state;

    /* Number of points */
    int length;

    /* Classification */
    char *classification;

    /* Whether the flash been reported */
    int is_reported;

    /* Records */
    struct list_head records;
} object_str;

object_str *object_create();
void object_delete(object_str *);
void object_add_record(object_str *, record_str *);

object_str *object_copy(object_str *);

char *object_params_as_hstore(object_str *);

double object_max_flux(object_str *);

int object_is_noise(object_str *);

int number_of_flashes(struct list_head *);

#endif /* REALTIME_TYPES_H */
