#ifndef FIELD_H
#define FIELD_H

#include "lists.h"
#include "stars.h"
#include "image.h"
#include "coords.h"
#include "kdtree.h"

/* Filters */
#define FIELD_FILTER_CLEAR 0
#define FIELD_FILTER_B 1
#define FIELD_FILTER_V 2
#define FIELD_FILTER_R 3

typedef struct {
    /* Original stars - as an array */
    star_str *stars;
    int Nstars;

    /* Coordinate transformation */
    coords_str coords;

    /* kd-tree */
    struct kdtree *kd;

    /* Photometric calibration */
    /* double C; */
    /* double Cb; */
    /* double Cv; */
    /* double Cr; */
    /* double Csigma; */

    /* double C[8]; */

    /* Some statistics */
    double mag_min;
    double mag_max;
    double mag_limit;
    double mag_limit_err;

    /* Filter used for calibration */
    int filter; /* 0 = Clear/Unknown, 1 = B, 2 = V, 3 = R */

    char *catalogue;

    /* Mean colors of the field stars */
    double BminusV;
    double VminusR;

    /* Max positional error */
    double max_dist;

    /* Debug outputs */
    int verbose;
} field_str;

field_str *field_create();
void field_delete(field_str *);

void field_set_filter(field_str *, char *);

void field_set_stars(field_str *, struct list_head *, char *);
void field_get_brightest_stars(field_str *, int , struct list_head *);
coords_str field_match_coords(field_str *, struct list_head *);

star_str *field_get_nearest_star(field_str *, double , double );
star_str *field_get_nearest_similar_star(field_str *, double , double , double );

void field_calibrate(field_str *, struct list_head *);

void field_annotate_image(field_str *, image_str *);

#endif /*FIELD_H */
