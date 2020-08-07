#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <xmmintrin.h>
#include <emmintrin.h>

#include "time_str.h"
#include "image.h"
#include "average.h"
#include "utils.h"
#include "lists.h"

average_image_str *average_image_create(int nominal_length)
{
    average_image_str *avg = (average_image_str *)malloc(sizeof(average_image_str));

    avg->length = 0;
    avg->width = 0;
    avg->height = 0;
    avg->exposure = 0;

    avg->nominal_length = nominal_length;

    avg->time_first = time_zero();
    avg->time_last = time_zero();

    avg->median_length = 0;

    avg->sum  = NULL;

    avg->median = NULL;

    avg->image0 = image_create(1, 1);
    return avg;
}

void average_image_delete(average_image_str *avg)
{
    if(!avg)
        return;

    if(avg->sum)
        free(avg->sum);
    if(avg->median)
        free(avg->median);

    if(avg->image0)
        image_delete(avg->image0);

    free(avg);
}

void average_image_reset(average_image_str *avg)
{
    if(!avg || !avg->median_length)
        return;

    avg->sum = realloc(avg->sum, 0);
    avg->median = realloc(avg->median, 0);

    avg->length = 0;
    avg->median_length = 0;
    avg->exposure = 0;
}

void average_image_reset_average(average_image_str *avg)
{
    if(!avg || !avg->length)
        return;

    memset(avg->sum, 0, avg->width*avg->height*sizeof(int));

    avg->length = 0;
    avg->exposure = 0;

    avg->time_first = time_zero();
    avg->time_last = time_zero();
}

int average_image_is_full(average_image_str *avg)
{
    if(avg &&
       avg->length >= avg->nominal_length)
        return TRUE;
    else
        return FALSE;
}

int average_image_is_median_usable(average_image_str *avg)
{
    if(avg &&
       avg->median_length >= avg->nominal_length)
        return TRUE;
    else
        return FALSE;
}

/* WARNING: time-critical function!!! */
void average_image_add(average_image_str *avg, image_str *image)
{
    int length;
    int d;

    if(!avg || !image)
        return;

    length = image->width*image->height;

    /* FIXME: maybe its' too suboptimal to reset all arrays on reset? */
    if(!avg->median_length){
        /* First image, need to create necessary arrays */
        avg->width = image->width;
        avg->height = image->height;

        avg->sum  = (int *)calloc(length, sizeof(int));

        avg->median  = (int *)calloc(2*length, sizeof(int));
    }

    /* Update running sums and median estimates */
    if(avg->median_length > 50){
        for(d = 0; d < length; d++){
            u_int16_t value = image->data[d];
            /* Update the previously acquired estimation */
            /* float v1 = INV_AVGSCALE*value - avg->median[2*d]; */
            /* float v2 = 1.4826*fabsf(v1) /\* (v1 > 0 ? v1 : (v1 < 0 ? -v1 : 0)) *\/ - avg->median[2*d + 1]; */

            int v1 = INV_AVGSCALE_INT*value - avg->median[2*d];
            int v2 = 148*abs(v1) - 100*avg->median[2*d + 1];
            int step = 1;//(avg->median[2*d + 1] >> 2) + 1;

            avg->sum[d] += value;

            /* Hacks from http://graphics.stanford.edu/~seander/bithacks.html#CopyIntegerSign */
            avg->median[2*d] += step*((v1 != 0) | (v1 >> (sizeof(int) * CHAR_BIT - 1)));
            //avg->median[2*d] += (v1 > 0) - (v1 < 0); // (v1 > 0 ? 1 : (v1 < 0) ? -1 : 0);
            //step = (step >> 2) + 1;
            avg->median[2*d + 1] += step*((v2 != 0) | (v2 >> (sizeof(int) * CHAR_BIT - 1)));
            //avg->median[2*d + 1] += (v2 > 0) - (v2 < 0); //(v2 > 0 ? 1 : (v2 < 0) ? -1 : 0);
        }
    } else if(avg->median_length == 50) {
        for(d = 0; d < length; d++){
            int value = image->data[d];
            double s = 0;
            double s2 = 0;

            avg->sum[d]  += value;

            s = avg->sum[d];
            s2 = ((double *)avg->median)[d] + 1.0*value*value;

            /* Estimate from sample mean and stddev */
            avg->median[2*d] = INV_AVGSCALE*s/(avg->length + 1);
            avg->median[2*d + 1] = INV_AVGSCALE*sqrt((s2 - 1.0*s*s/(avg->length + 1))/avg->length);
        }
    }  else
        for(d = 0; d < length; d++){
            int value = image->data[d];

            avg->sum[d]  += value;
            ((double *)avg->median)[d] += 1.0*value*value; /* We will use it as a sum2 replacement until 50th frame */
        }

    if(!avg->length)
        avg->time_first = image->time;

    avg->length ++;
    avg->median_length ++;
    avg->exposure += image_keyword_get_double(image, "EXPOSURE");

    avg->time_last = image->time;

    image_copy_properties(image, avg->image0);
}

static void average_image_set_keywords(average_image_str *avg, image_str *image)
{
    /* Total time span of average frame, including any possible gaps */
    double span = 1e-3*time_interval(avg->time_first, avg->time_last) + image_keyword_get_double(avg->image0, "EXPOSURE");
    time_str midtime = time_incremented(avg->time_first, 0.5*span);

    image_keyword_add_double(image, "EXPOSURE", avg->exposure/avg->length, "Normalized (average) exposure");
    image_keyword_add_double(image, "TOTAL EXPOSURE", avg->exposure, "Total exposure of co-added frames");
    image_keyword_add_int(image, "AVERAGED", avg->length, "Total number of frames coadded");
    image_keyword_add_time(image, "MIDTIME", midtime, "Mid-time of covered time span");
}

image_str *average_image_mean(average_image_str *avg)
{
    image_str *image = NULL;

    if(avg && avg->length && avg->width && avg->height){
        int d;

        image = image_create_double(avg->width, avg->height);

        for(d = 0; d < image->width*image->height; d++)
            image->double_data[d] = avg->sum[d]*1./avg->length;

        image_copy_properties(avg->image0, image);
        average_image_set_keywords(avg, image);
    }

    return image;
}

image_str *average_image_median(average_image_str *avg)
{
    image_str *image = NULL;

    if(avg && avg->length && avg->width && avg->height){
        int d;

        image = image_create_double(avg->width, avg->height);

        for(d = 0; d < image->width*image->height; d++)
            image->double_data[d] = avg->median[2*d]*AVGSCALE;

        image_copy_properties(avg->image0, image);
        average_image_set_keywords(avg, image);
    }

    return image;
}

image_str *average_image_sigma(average_image_str *avg)
{
    image_str *image = NULL;

    if(avg && avg->length && avg->width && avg->height){
        int d;

        image = image_create_double(avg->width, avg->height);

        for(d = 0; d < image->width*image->height; d++)
            image->double_data[d] = avg->median[2*d + 1]*AVGSCALE;

        image_copy_properties(avg->image0, image);
        average_image_set_keywords(avg, image);
    }

    return image;
}

image_str *average_image_excess_image(average_image_str *avg, image_str *image)
{
    image_str *excess = NULL;

    if(avg && avg->length && avg->width && avg->height){
        int d;

        excess = image_create_double(avg->width, avg->height);

        if(image->type == IMAGE_DOUBLE)
            for(d = 0; d < image->width*image->height; d++)
                excess->double_data[d] = (image->double_data[d] - AVGSCALE*avg->median[2*d])/avg->median[2*d + 1]/AVGSCALE;
        else
            for(d = 0; d < image->width*image->height; d++)
                excess->double_data[d] = (image->data[d] - AVGSCALE*avg->median[2*d])/avg->median[2*d + 1]/AVGSCALE;

        image_copy_properties(avg->image0, excess);
        average_image_set_keywords(avg, excess);
    }

    return excess;
}

void average_image_dump_to_fits(average_image_str *avg, char *filename_mean, char *filename_sigma)
{
    image_str *mean = average_image_mean(avg);
    image_str *sigma = average_image_sigma(avg);

    image_dump_to_fits(mean, filename_mean);
    image_dump_to_fits(sigma, filename_sigma);

    image_delete(mean);
    image_delete(sigma);
}
