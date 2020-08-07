#ifndef RECORDS_H
#define RECORDS_H

#include "lists.h"
#include "time_str.h"
#include "coords.h"

#define RECORD_SATURATED 0x01
#define RECORD_TRUNCATED 0x02
#define RECORD_BLENDED 0x04
#define RECORD_ELONGATED 0x08
#define RECORD_MERGED 0x10
#define RECORD_NOISE 0x20
#define RECORD_UNRELIABLE_COORDS 0x40
#define RECORD_BAD 0x100

/* Basic structure for an appearance of an object on the frame */
/* Will be used both in realtime and second scale subsystems */
typedef struct record_str {
    LIST_HEAD(struct record_str);

    /* Time and frame number */
    time_str time;
    /* u_int64_t frame_number; */

    /* Position */
    double x; /* Center */
    double y;

    /* Position uncertainty */
    double sigma_x;
    double sigma_y;

    /* Elliptic shape */
    double a; /* Major axis */
    double b; /* Minor axis */
    double theta; /* Major axis angle */

    /* Brightness */
    double flux; /* Counts - average */
    double flux_err; /* Uncertainty of flux */

    /* Significance */
    double excess;

    /* Sky position */
    double ra;
    double dec;

    /* Magnitudes */
    double mag;
    double mag_err;

    /* Flags */
    int flags;
} record_str;

typedef struct record_extended_str {
    /* Structure inheritance */
    struct record_str;

    char *filter;
} record_extended_str;

record_str record_create();
record_str *record_create_ptr();
void record_delete(record_str *);
void record_extended_delete(record_extended_str *);

record_str *record_copy(record_str *);

void records_list_get_brightest_records(struct list_head *, int , struct list_head *);

#endif /* RECORDS_H */
