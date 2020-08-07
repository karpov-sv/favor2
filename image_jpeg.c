#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jpeglib.h>
#include <math.h>

#include "utils.h"

#include "image.h"

static int jpeg_scale = 4;
static double jpeg_minp = 0.03;
static double jpeg_maxp = 0.95;

void image_jpeg_set_scale(int scale)
{
    jpeg_scale = scale;
}

void image_jpeg_set_percentile(double pmin, double pmax)
{
    jpeg_minp = MAX(0, pmin);
    jpeg_maxp = MIN(1, pmax);
}

void image_percentile_norm(image_str *image, double min_p, double max_p, void *min_ptr, void *max_ptr)
{
    int step = 100;
    int N = image->width*image->height;
    int *idx = (int *)malloc(sizeof(int)*N/step);
    int d;

    for(d = 0; d < N/step; d++)
        idx[d] = d*step;

    if(image->type == IMAGE_DOUBLE){
        int compare_fn(const void *v1, const void *v2)
        {
            double res = image->double_data[*(int *)v1] - image->double_data[*(int *)v2];

            return (res > 0 ? 1 : (res < 0 ? -1 : 0));
        }

        qsort(idx, N/step, sizeof(int), compare_fn);

        *((double *)min_ptr) = image->double_data[idx[(int)floor(min_p*(N/step - 1))]];
        *((double *)max_ptr) = image->double_data[idx[(int)floor(max_p*(N/step - 1))]];
    } else {
        int compare_fn(const void *v1, const void *v2)
        {
            int res = image->data[*(int *)v1] - image->data[*(int *)v2];

            return (res > 0 ? 1 : (res < 0 ? -1 : 0));
        }

        qsort(idx, N/step, sizeof(int), compare_fn);

        *((int *)min_ptr) = image->data[idx[(int)floor(min_p*(N/step - 1))]];
        *((int *)max_ptr) = image->data[idx[(int)floor(max_p*(N/step - 1))]];
    }

    free(idx);
}

/* Integer bit hack from http://graphics.stanford.edu/~seander/bithacks.html#IntegerMinOrMax */
#define MIN_INT_OPT(x, y) ((y) ^ (((x) ^ (y)) & -((x) < (y))))
#define MAX_INT_OPT(x, y) ((x) ^ (((x) ^ (y)) & -((x) < (y))))

void image_jpeg_worker(image_str *image, char *filename, unsigned char **buffer_ptr, int *length_ptr)
{
    FILE *fd = NULL;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    int i, quality = 95, smoothness = 0, index = 0;
    int scale = jpeg_scale; /* FIXME: make adjustable */
    double min_p = jpeg_minp;
    double max_p = jpeg_maxp;
    JSAMPARRAY /* colormap, */ buffer;
    JSAMPROW ptr;
    int new_width;
    int new_height;
    int shift = 0;
    int d;

    if(scale == 1)
        shift = 0;
    else if(scale <= 2)
        shift = 1;
    else if(scale <= 4)
        shift = 2;
    else if(scale <= 8)
        shift = 3;

    new_width = image->width >> shift;
    new_height = image->height >> shift;

    char *data = calloc(new_width*new_height, sizeof(char));
    long unsigned int length = 0;

    /* Scale the data */
    if(image->type == IMAGE_DOUBLE){
        double min = image->double_data[0];
        double max = image->double_data[0];
        int x;
        int y;

        image_percentile_norm(image, min_p, max_p, &min, &max);

        /* Safety check */
        if(max <= min)
            max = min + 1;

        d = 0;

        for(y = 0; y < image->height; y++)
            for(x = 0; x < image->width; x++){
                int value = 255.0*(image->double_data[d++] - min)/(max - min);
                int new_x = x >> shift;
                int new_y = y >> shift;

                value = MIN_INT_OPT(MAX_INT_OPT(0, value), 255) >> shift*2;

                data[new_x + new_y*new_width] += value;
            }
    } else {
        int min = image->data[0];
        int max = image->data[0];
        int x;
        int y;

        image_percentile_norm(image, min_p, max_p, &min, &max);

        /* Safety check */
        if(max <= min)
            max = min + 1;

        d = 0;

        for(y = 0; y < image->height; y++)
            for(x = 0; x < image->width; x++){
                int value = 255*(image->data[d++] - min)/(max - min);
                int new_x = x >> shift;
                int new_y = y >> shift;

                value = MIN_INT_OPT(MAX_INT_OPT(0, value), 255) >> shift*2;

                data[new_x + new_y*new_width] += value;
            }
    }

    /* Initialize the JPEG compression object with default error handling. */
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    /* Initialize JPEG parameters which may be overridden later */
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);

    cinfo.input_components = 1;
    cinfo.in_color_space = JCS_GRAYSCALE;

    cinfo.image_width = new_width;
    cinfo.image_height = new_height;
    cinfo.data_precision = BITS_IN_JSAMPLE;
    buffer = (*cinfo.mem->alloc_sarray) ((j_common_ptr) & cinfo, JPOOL_IMAGE,
                                         (JDIMENSION) (cinfo.image_width * cinfo.input_components), (JDIMENSION) 1);

    /* Fix colorspace-dependent defaults */
    jpeg_default_colorspace(&cinfo);

    /* Customize parameters */
    cinfo.smoothing_factor = smoothness;
    jpeg_set_quality(&cinfo, quality, TRUE);

    /* Specify data destination for compression */
    if(filename){
        fd = (*filename == '-') ? stdout : fopen(filename, "w");
        jpeg_stdio_dest(&cinfo, fd);
    } else {
        jpeg_mem_dest(&cinfo, buffer_ptr, &length);
    }

    /* Start compressor */
    jpeg_start_compress(&cinfo, TRUE);

    /* Process JPEG scanlines */
    while (cinfo.next_scanline < cinfo.image_height){
        ptr = buffer[0];
        for (i = 0; i < (int) cinfo.image_width; i++){
            *ptr++ = (unsigned char) data[index++];
        }

        (void) jpeg_write_scanlines(&cinfo, buffer, 1);

    }

    /* Finish compression and release memory */
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    free(data);

    if(filename){
        if(fd != stdout)
            fclose(fd);
    } else {
        if(length_ptr)
            *length_ptr = length;
    }
}

void image_dump_to_jpeg(image_str *image, char *filename)
{
    image_jpeg_worker(image, filename, NULL, NULL);
}

void image_convert_to_jpeg(image_str *image, unsigned char **buffer_ptr, int *length_ptr)
{
    image_jpeg_worker(image, NULL, buffer_ptr, length_ptr);
}
