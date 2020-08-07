#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "utils.h"
#include "time_str.h"
#include "image.h"

#include "sextractor.h"

char *temp_fits_name(image_str *image)
{
    char *filename = make_temp_filename("/tmp/sex_img_XXXXXX");

    /* FIXME: correct for dark and flatfield */
    image_dump_to_fits(image, filename);

    return filename;
}

char *temp_param_name()
{
    char *filename = make_temp_filename("/tmp/sex_param_XXXXXX");
    FILE *file = NULL;

    file = fopen(filename, "w");

    fprintf(file,
            "MAG_AUTO\n"
            "X_IMAGE\nY_IMAGE\n"
            /* "XWIN_IMAGE\nYWIN_IMAGE\n" */
            "A_IMAGE\nB_IMAGE\nTHETA_IMAGE\n"
            "FLAGS\n"
            "FLUX_AUTO\n"
            "FLUXERR_AUTO\n"
            /* "ERRX2_IMAGE\nERRY2_IMAGE\n" */
            "ERRX2WIN_IMAGE\nERRY2WIN_IMAGE\n"
            "FLUX_APER\nFLUXERR_APER\n"
            );

    fclose(file);

    return filename;
}

char *temp_param_name_fwhm()
{
    char *filename = make_temp_filename("/tmp/sex_param_XXXXXX");
    FILE *file = NULL;

    file = fopen(filename, "w");

    fprintf(file,
            "FWHM_IMAGE\n"
            "FLAGS\n"
            );

    fclose(file);

    return filename;
}

char *temp_empty_name()
{
    char *filename = make_temp_filename("/tmp/sex_empty_XXXXXX");
    FILE *file = NULL;

    file = fopen(filename, "w");

    fclose(file);

    return filename;
}

char *temp_filter_name()
{
    char *filename = make_temp_filename("/tmp/sex_filter_XXXXXX");
    FILE *file = NULL;

    file = fopen(filename, "w");

    /* gauss_3.0_7x7.conv */
    /* fprintf(file, "CONV NORM\n" */
    /*         "# 7x7 convolution mask of a gaussian PSF with FWHM = 3.0 pixels.\n" */
    /*         "0.004963 0.021388 0.051328 0.068707 0.051328 0.021388 0.004963\n" */
    /*         "0.021388 0.092163 0.221178 0.296069 0.221178 0.092163 0.021388\n" */
    /*         "0.051328 0.221178 0.530797 0.710525 0.530797 0.221178 0.051328\n" */
    /*         "0.068707 0.296069 0.710525 0.951108 0.710525 0.296069 0.068707\n" */
    /*         "0.051328 0.221178 0.530797 0.710525 0.530797 0.221178 0.051328\n" */
    /*         "0.021388 0.092163 0.221178 0.296069 0.221178 0.092163 0.021388\n" */
    /*         "0.004963 0.021388 0.051328 0.068707 0.051328 0.021388 0.004963\n"); */

    /* gauss_2.0_5x5.conv */
    fprintf(file, "CONV NORM\n"
            "# 5x5 convolution mask of a gaussian PSF with FWHM = 2.0 pixels.\n"
            "0.006319 0.040599 0.075183 0.040599 0.006319\n"
            "0.040599 0.260856 0.483068 0.260856 0.040599\n"
            "0.075183 0.483068 0.894573 0.483068 0.075183\n"
            "0.040599 0.260856 0.483068 0.260856 0.040599\n"
            "0.006319 0.040599 0.075183 0.040599 0.006319\n");
    /* gauss_2.5_5x5.conv */
    /* fprintf(file, "CONV_NORM\n" */
    /*         "# 5x5 convolution mask of a gaussian PSF with FWHM = 2.5 pixels.\n" */
    /*         "0.034673 0.119131 0.179633 0.119131 0.034673\n" */
    /*         "0.119131 0.409323 0.617200 0.409323 0.119131\n" */
    /*         "0.179633 0.617200 0.930649 0.617200 0.179633\n" */
    /*         "0.119131 0.409323 0.617200 0.409323 0.119131\n" */
    /*         "0.034673 0.119131 0.179633 0.119131 0.034673\n"); */

    fclose(file);

    return filename;
}

void sextractor_get_stars_list_aper(image_str *image, int number, struct list_head *records, double aper_diam)
{
    double saturation = image_max_value(image);
    char *fits_filename = temp_fits_name(image);
    char *cat_filename = make_temp_filename("/tmp/sex_cat_XXXXXX");
    char *param_filename = temp_param_name();
    char *filter_filename = temp_filter_name();
    char *empty_filename = temp_empty_name();
    FILE *file;
    int current = 0;

    system_run_silently("sex %s -c %s -CATALOG_TYPE ASCII -PARAMETERS_NAME %s"
                        " -DETECT_MINAREA 5 -FILTER Y -FILTER_NAME %s"
                        " -BACK_SIZE 256 -BACKPHOTO_TYPE LOCAL"
                        " -DETECT_THRESH 4.0 -DETECT_MINAREA 4 -ANALYSIS_THRESH 0.5 -SATUR_LEVEL %g"
                        " -PIXEL_SCALE 16.0 -SEEING_FWHM 20.0"
                        " -DEBLEND_NTHRESH 64 -DEBLEND_MINCONT 0.001"
                        " -PHOT_APERTURES %g"
                        " -CLEAN Y -VERBOSE_TYPE QUIET"
                        " -CATALOG_NAME %s",
                        fits_filename, empty_filename, param_filename,
                        filter_filename,
                        saturation, aper_diam, cat_filename);

    init_list(*records);

    file = fopen(cat_filename, "r");

    while(!feof(file) && current < number){
        record_str *record = record_create_ptr();
        double tmp = 0;
        int flags = 0;
        int nres = 0;
        double flux = 0;
        double flux_err = 0;

        nres = fscanf(file, "%lf %lf %lf %lf %lf %lf %d %lf %lf %lf %lf %lf %lf\n",
                      &tmp, &record->x, &record->y, &record->a, &record->b,
                      &record->theta, &flags, &record->flux, &record->flux_err,
                      &record->sigma_x, &record->sigma_y, &flux, &flux_err);

        /* Fixed aperture flux */
        if(aper_diam > 0){
            record->flux = flux;
            record->flux_err = flux_err;
        }

        if(nres > 5 && record->flux > 0){
            /* Sextractor (actually, FITS standard) coords base is (1, 1),
               while our is (0, 0) */
            record->x -= 1;
            record->y -= 1;

            record->sigma_x = sqrt(record->sigma_x);
            record->sigma_y = sqrt(record->sigma_y);

            record->excess = record->flux/record->flux_err;

            record->time = image->time;

            if(flags & 1)
                record->flags |= RECORD_BAD;
            if(flags & 2)
                record->flags |= RECORD_BLENDED;
            if(flags & 4)
                record->flags |= RECORD_SATURATED;
            if(flags & 8)
                record->flags |= RECORD_TRUNCATED;

            /* Simple estimation for object elongation */
            if((record->b > 1 && record->a > 5*record->b) || record->a > 10)
                record->flags |= RECORD_ELONGATED;

            add_to_list_end(*records, record);

            current ++;
        }
    }

    fclose(file);

    dprintf("%d stars extracted\n", current);

    unlink(fits_filename);
    unlink(cat_filename);
    unlink(param_filename);
    unlink(filter_filename);
    unlink(empty_filename);

    free(fits_filename);
    free(cat_filename);
    free(param_filename);
    free(filter_filename);
    free(empty_filename);
}

void sextractor_get_stars_list(image_str *image, int number, struct list_head *records)
{
    sextractor_get_stars_list_aper(image, number, records, 0);
}

double sextractor_get_fwhm(image_str *image)
{
    double saturation = image_max_value(image);
    char *fits_filename = temp_fits_name(image);
    char *cat_filename = make_temp_filename("/tmp/sex_cat_XXXXXX");
    char *param_filename = temp_param_name_fwhm();
    char *filter_filename = temp_filter_name();
    char *empty_filename = temp_empty_name();
    double *fwhms = NULL;
    int N = 0;
    FILE *file;

    system_run_silently("sex %s -c %s -CATALOG_TYPE ASCII -PARAMETERS_NAME %s"
                        " -BACK_SIZE 256 -BACKPHOTO_TYPE LOCAL"
                        " -CLEAN Y -VERBOSE_TYPE QUIET -SATUR_LEVEL %g"
                        " -CATALOG_NAME %s",
                        fits_filename, empty_filename, param_filename,
                        saturation, cat_filename);

    file = fopen(cat_filename, "r");

    while(!feof(file)){
        double fwhm = 0;
        int flags = 0;

        if(fscanf(file, "%lf %d", &fwhm, &flags) > 0){
            if(flags == 0){
                fwhms = realloc(fwhms, sizeof(double)*(N + 1));
                fwhms[N] = fwhm;
                N ++;
            }
        }
    }

    fclose(file);

    unlink(fits_filename);
    unlink(cat_filename);
    unlink(param_filename);
    unlink(filter_filename);
    unlink(empty_filename);

    free(fits_filename);
    free(cat_filename);
    free(param_filename);
    free(filter_filename);
    free(empty_filename);

    return get_median(fwhms, N);
}
