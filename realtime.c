#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_multifit.h>

#include "utils.h"
#include "realtime.h"
#include "realtime_types.h"

realtime_str *realtime_create(int nominal)
{
    realtime_str *realtime = (realtime_str *)malloc(sizeof(realtime_str));

    realtime->avg = average_image_create(nominal);

    realtime->coords = coords_empty();

    init_list(realtime->objects);

    realtime->saturation_level = 65535;
    realtime->initial_catch_radius = 20;
    realtime->max_meteor_angle = 20;
    realtime->meteor_elongation_threshold = 5;
    realtime->meteor_length_threshold = 20;
    realtime->flash_reporting_interval = 0.8;
    realtime->flash_invisibility_interval = 0.5;
    realtime->moving_invisibility_interval = 1;
    realtime->sigma_correction = 1;

    realtime->max_records_per_frame = 1000;

    realtime->gain = 1;

    realtime->badpix = NULL;
    realtime->dark = NULL;
    realtime->flat = NULL;

    realtime->mean_num_records = 0;

    realtime->object_counter = 0;

    /* By default we will not process the data without coordinate transform provided */
    realtime->should_work_without_coords = FALSE;

    realtime->hlength = 0;
    realtime->hcoords = NULL;
    realtime->htime = NULL;

    REALTIME_SET_HOOK(realtime, records_extracted_hook, NULL, NULL);
    REALTIME_SET_HOOK(realtime, objects_iterated_hook, NULL, NULL);
    REALTIME_SET_HOOK(realtime, object_finished_hook, NULL, NULL);
    REALTIME_SET_HOOK(realtime, flash_detected_hook, NULL, NULL);
    REALTIME_SET_HOOK(realtime, flash_cancelled_hook, NULL, NULL);

    return realtime;
}

void realtime_delete(realtime_str *realtime)
{
    object_str *object = NULL;

    if(!realtime)
        return;

    foreach(object, realtime->objects){
        del_from_list_in_foreach_and_run(object, object_delete(object));
    }

    average_image_delete(realtime->avg);

    free_and_null(realtime->hcoords);
    free_and_null(realtime->htime);

    /* In most use cases these are provided and managed externally */
    /* if(realtime->badpix) */
    /*     image_delete(realtime->badpix); */

    /* if(realtime->flat) */
    /*     image_delete(realtime->flat); */

    free(realtime);
}

void realtime_reset(realtime_str *realtime)
{
    object_str *object = NULL;

    /* Delete all objects */
    foreach(object, realtime->objects){
        /* We should report all objects that are still being tracked */
        if((object->state == OBJECT_FLASH && object->is_reported) ||
           object->state == OBJECT_MOVING ||
           object->state == OBJECT_METEOR)
            REALTIME_CALL_HOOK(realtime, object_finished_hook, object);

        del_from_list_in_foreach_and_run(object, object_delete(object));
    }

    /* Coordinates are unreliable after reset and should be provided again */
    realtime->coords = coords_empty();

    realtime->hlength = 0;
    free_and_null(realtime->hcoords);
    free_and_null(realtime->htime);

    realtime->mean_num_records = 0;

    average_image_reset(realtime->avg);
}

void realtime_add_coords(realtime_str *realtime, coords_str coords, time_str time)
{
    realtime->hcoords = realloc(realtime->hcoords, sizeof(coords_str)*(realtime->hlength + 1));
    realtime->htime = realloc(realtime->htime, sizeof(time_str)*(realtime->hlength + 1));

    realtime->hcoords[realtime->hlength] = coords;
    realtime->htime[realtime->hlength] = time;

    realtime->hlength ++;

    /* Should we really set the coordinates here? Or the user should manage it? */
    realtime->coords = coords;
}

#if 0 /* Global polynomial fit */
void realtime_fix_object_coords(realtime_str *realtime, object_str *object)
{
    int order = 2;
    record_str *record = NULL;
    int N = realtime->hlength;

    /* FIXME: arbitrarily chosen numbers */
    if(N > 50)
        order = 3;

    if(N >= order + 1){
        gsl_multifit_linear_workspace *work  = gsl_multifit_linear_alloc(N, order + 1);
        gsl_matrix *T = gsl_matrix_alloc(N, order + 1);
        gsl_vector *xi = gsl_vector_alloc(N);
        gsl_vector *eta = gsl_vector_alloc(N);
        gsl_vector *Cxi = gsl_vector_alloc(order + 1);
        gsl_vector *Ceta = gsl_vector_alloc(order + 1);
        gsl_matrix *covxi = gsl_matrix_alloc(order + 1, order + 1);
        gsl_matrix *coveta = gsl_matrix_alloc(order + 1, order + 1);
        double chisq = 0;

        coords_str coords0 = realtime->hcoords[0];
        time_str time0 = realtime->htime[0];
        double exposure = image_keyword_get_double(realtime->avg->image0, "EXPOSURE");
        int d;

        foreachback(record, object->records){
            double dt = 1e-3*time_interval(time0, record->time) + 0.5*exposure;
            double xi1;
            double eta1;

            for(d = 0; d < N; d++){
                double ra;
                double dec;
                double t1 = 1e-3*time_interval(time0, realtime->htime[d]);
                double coeff = 1;
                int dd;

                for(dd = 0; dd <= order; dd++){
                    gsl_matrix_set(T, d, dd, coeff);
                    coeff *= t1;
                }

                coords_get_ra_dec(&realtime->hcoords[d], record->x, record->y, &ra, &dec);
                coords_get_xi_eta_from_ra_dec(&coords0, ra, dec, &xi1, &eta1);

                gsl_vector_set(xi, d, xi1);
                gsl_vector_set(eta, d, eta1);
            }

            gsl_multifit_linear(T, xi, Cxi, covxi, &chisq, work);
            gsl_multifit_linear(T, eta, Ceta, coveta, &chisq, work);

            {
                double coeff = 1;
                int dd;

                xi1 = 0;
                eta1 = 0;

                for(dd = 0; dd <= order; dd++){
                    xi1 += coeff*gsl_vector_get(Cxi, dd);
                    eta1 += coeff*gsl_vector_get(Ceta, dd);
                    coeff *= dt;
                }
            }

            if(isfinite(xi1) && isfinite(eta1)){
                double ra0 = record->ra;
                double dec0 = record->dec;

                coords_get_ra_dec_from_xi_eta(&coords0, xi1, eta1, &record->ra, &record->dec);

                dprintf("%g - %g %g - %g\n", dt, record->ra - ra0, record->dec - dec0, coords_sky_distance(ra0, dec0, record->ra, record->dec)*3600);
            } else
                record->flags |= RECORD_UNRELIABLE_COORDS;
        }

        gsl_matrix_free(T);
        gsl_vector_free(xi);
        gsl_vector_free(eta);
        gsl_vector_free(Cxi);
        gsl_vector_free(Ceta);
        gsl_matrix_free(covxi);
        gsl_matrix_free(coveta);
    } else {
        foreachback(record, object->records)
            record->flags |= RECORD_UNRELIABLE_COORDS;
    }
}
#else /* Interpolation */
void realtime_fix_object_coords(realtime_str *realtime, object_str *object)
{
    record_str *record = NULL;
    int N = realtime->hlength;

    if(N < 2){
        foreachback(record, object->records)
            record->flags |= RECORD_UNRELIABLE_COORDS;

        return;
    } else {
        coords_str coords0 = realtime->hcoords[0];
        time_str time0 = realtime->htime[0];
        double exposure = image_keyword_get_double(realtime->avg->image0, "EXPOSURE");

        foreachback(record, object->records){
            double t0 = 1e-3*time_interval(time0, record->time) + 0.5*exposure;
            double t1;
            double t2;
            double xi1;
            double eta1;
            double xi2;
            double eta2;
            double ra;
            double dec;

            int i = 0;

            for(i = 0; i < N-2; i++)
                if(time_interval(record->time, realtime->htime[i+1]) >= 0)
                    break;

            t1 = 1e-3*time_interval(time0, realtime->htime[i]);
            t2 = 1e-3*time_interval(time0, realtime->htime[i+1]);

            coords_get_ra_dec(&realtime->hcoords[i], record->x, record->y, &ra, &dec);
            coords_get_xi_eta_from_ra_dec(&coords0, ra, dec, &xi1, &eta1);

            coords_get_ra_dec(&realtime->hcoords[i+1], record->x, record->y, &ra, &dec);
            coords_get_xi_eta_from_ra_dec(&coords0, ra, dec, &xi2, &eta2);

            coords_get_ra_dec_from_xi_eta(&coords0,
                                          xi1 + (xi2 - xi1)*(t0 - t1)/(t2 - t1),
                                          eta1 + (eta2 - eta1)*(t0 - t1)/(t2 - t1),
                                          &record->ra, &record->dec);
        }
    }
}
#endif

void realtime_process_frame(realtime_str *realtime, image_str *image)
{
    realtime->time = image->time;

    /* Check whether the average frame is expired */
    /* TODO: get the limiting delay dynamically */
    /* TODO: should we also cleanup objects here?.. */
    if(realtime->avg->length && 1e-3*time_interval(realtime->avg->time_last, image->time) > 12){
        dprintf("Average frame is %g s old, resetting it\n", 1e-3*time_interval(realtime->avg->time_last, image->time));
        average_image_reset(realtime->avg);
    }

    if(average_image_is_median_usable(realtime->avg) &&
       (!coords_is_empty(&realtime->coords) || realtime->should_work_without_coords)){
        int nrecords = 0;

        init_list(realtime->records);

        /* Extract records from the frame */
        realtime_find_records(realtime, image);

        nrecords = list_length(&realtime->records);

        if(!realtime->mean_num_records)
            realtime->mean_num_records = nrecords;
        else
            realtime->mean_num_records += 0.01*(nrecords - realtime->mean_num_records);

        if(nrecords < realtime->max_records_per_frame &&
           fabs(nrecords - realtime->mean_num_records) < 3.0*sqrt(realtime->mean_num_records)){
            /* Report them to user-defined hook, if any */
            REALTIME_CALL_HOOK(realtime, records_extracted_hook);

            /* Classify records */
            realtime_classify_records(realtime);

            /* Get rid of finished objects */
            realtime_cleanup_objects(realtime);

            /* Adjust some tunable parameters depending on sky conditions */
            /* realtime_adjust_parameters(realtime); */

            REALTIME_CALL_HOOK(realtime, objects_iterated_hook);
        } else if (fabs(nrecords - realtime->mean_num_records) > 5.0*sqrt(realtime->mean_num_records)){
            /* dprintf("resetting %s: %d %g %g\n", time_str_get_date_time(image->time), nrecords, realtime->mean_num_records, 5.0*sqrt(realtime->mean_num_records)); */
            realtime_reset(realtime);
        }

        /* Get rid of records which are still in the list */
        free_list(realtime->records);
    }

    average_image_add(realtime->avg, image);
}
