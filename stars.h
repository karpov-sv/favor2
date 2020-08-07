#ifndef STAR_H
#define STAR_H

#include "lists.h"

typedef struct star_str {
    LIST_HEAD(struct star_str);

    /* Position on the sky */
    double ra;
    double dec;

    /* Brightness */
    double b;
    double v;
    double r;

    /* Standard coordinates */
    double xi;
    double eta;

    /* Auxiliary fields */
    int count;
    double mag;
} star_str;

star_str star_create();
star_str *star_create_ptr();

star_str *star_copy(star_str *);

void star_delete(star_str *);

void tycho2_get_stars_list(double , double , double , int , struct list_head *);
void nomad_get_stars_list(double , double , double , int , struct list_head *);
void wbvr_get_stars_list(double , double , double , int , struct list_head *);
void pickles_get_stars_list(double , double , double , int , struct list_head *);

#endif /* STAR_H */
