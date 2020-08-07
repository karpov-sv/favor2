#ifndef AVERAGE_H
#define AVERAGE_H

#include "image.h"

/* Basically, the step we use to change the median estimation */
#define AVGSCALE 0.1
#define INV_AVGSCALE 10.0
#define INV_AVGSCALE_INT 10

#define AVG_IDX(avg, x, y) ((avg)->width*(y) + (x))
#define MEDIAN(avg, x, y) (AVGSCALE*(avg)->median[2*AVG_IDX(avg, (x), (y))])
#define MAD(avg, x, y) (AVGSCALE*(avg)->median[2*AVG_IDX(avg, (x), (y)) + 1])
#define EXCESS(avg, image, x, y) (MAD((avg), (x), (y)) > 0 ? (PIXEL((image), (x), (y)) - MEDIAN((avg), (x), (y)))*1.0/MAD((avg), (x), (y)) : 0)

typedef struct {
    int length; /* Current number of frames added to running sums */
    int median_length; /* Frames used in median and MAD estimation, will increase until reset. */
    int width;
    int height;

    int nominal_length; /* Nominal length of the average */

    /* Time of first and last frames added */
    time_str time_first;
    time_str time_last;

    /* Cumulative exposure of average */
    double exposure;

    /* Cumulative sum, "length" frames */
    int *sum;

    /* Running median and its absolute deviation, in units of AVGSCALE */
    /* Interlaced storage, 2*i for median, 2*i+1 for MAD */
    int *median;

    /* Dummy image to keep all the keywords */
    image_str *image0;
} average_image_str;

average_image_str *average_image_create(int );
void average_image_delete(average_image_str *);
/* Complete reset, both median and average */
void average_image_reset(average_image_str *);
/* Reset average only, leave median intact */
void average_image_reset_average(average_image_str *);
int average_image_is_full(average_image_str *);
int average_image_is_median_usable(average_image_str *);
void average_image_add(average_image_str *, image_str *);
double average_image_excess(average_image_str *, image_str *, int , int );
/* Get average image */
image_str *average_image_mean(average_image_str *);
image_str *average_image_median(average_image_str *);
image_str *average_image_sigma(average_image_str *);
image_str *average_image_excess_image(average_image_str *, image_str *);
/* Save average image as FITS */
void average_image_dump_to_fits(average_image_str *, char *, char *);

#endif /* AVERAGE_H */
