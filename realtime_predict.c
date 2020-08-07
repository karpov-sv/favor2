#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_cdf.h>

#include "utils.h"
#include "realtime_predict.h"
#include "matrix.h"

#include "simulator_types.h"

//#define USE_WEIGHTS

void gsl_matrix_print(const gsl_matrix *m)
{
    size_t i;
    size_t j;

    for(i = 0; i < m->size1; i++){
        for(j = 0; j < m->size2; j++)
            dprintf("%g ", gsl_matrix_get(m, i, j));

        dprintf("\n");
    }
}

prediction_str realtime_predict_weighted(object_str *object, time_str time_current, int order, double initial_radius, double sigma_correction)
{
    prediction_str pr;
    int length = object->length;
    int i;
    int j;

    memset(object->Cx, 0, sizeof(double)*OBJECT_MAXPARAMS);
    memset(object->Cy, 0, sizeof(double)*OBJECT_MAXPARAMS);

    pr.is_motion = FALSE;

    /* Singular case, no extrapolation needed */
    if(object->length == 1){
        pr.x = ((record_str*)list_first_item(object->records))->x;
        pr.y = ((record_str*)list_first_item(object->records))->y;
        /* Box of initial motion detection */
        pr.dx = initial_radius;
        pr.dy = initial_radius;

        object->Cx[0] = pr.x;
        object->Cy[0] = pr.y;
    } else {
        gsl_multifit_linear_workspace * work  = gsl_multifit_linear_alloc(length, order + 1);
        gsl_matrix *T = gsl_matrix_alloc(length, order + 1);
        gsl_vector *X = gsl_vector_alloc(length);
        gsl_vector *Y = gsl_vector_alloc(length);
        gsl_vector *WX = gsl_vector_alloc(length);
        gsl_vector *WY = gsl_vector_alloc(length);
        gsl_vector *CX = gsl_vector_alloc(order + 1);
        gsl_vector *CY = gsl_vector_alloc(order + 1);
        gsl_matrix *covX = gsl_matrix_alloc(order + 1, order + 1);
        gsl_matrix *covY = gsl_matrix_alloc(order + 1, order + 1);
        double chisqX = 0;
        double chisqY = 0;

        record_str *record = NULL;
        double time = 0;
        double Dpos = 0; /* Mean position variance for motion check */
        double shift = 0;
        int is_shift = FALSE;

        i = 0;

        foreach(record, object->records){
            double coeff = 1;
            time = 1e-3*time_interval(object->time0, record->time);

            for(j = 0; j <= order; j++){
                gsl_matrix_set(T, i, j, coeff);
                coeff *= time;
            }

            /* if(!isfinite(record->sigma_x) || !isfinite(record->sigma_y)) */
            /*     exit(1); */

            gsl_vector_set(X, i, record->x);
            gsl_vector_set(Y, i, record->y);
            gsl_vector_set(WX, i, 1.0/record->sigma_x/record->sigma_x/sigma_correction/sigma_correction);
            gsl_vector_set(WY, i, 1.0/record->sigma_y/record->sigma_y/sigma_correction/sigma_correction);

            i ++;
        }

#ifdef USE_WEIGHTS
        gsl_multifit_wlinear(T, WX, X, CX, covX, &chisqX, work);
        gsl_multifit_wlinear(T, WY, Y, CY, covY, &chisqY, work);

        /* gsl_multifit_wlinear returns unnormalized covariance which assumes
           that sigmas we provide are exact variances. That's not always so. So
           let's normalize it to actual variance of residuals, if they are
           large enough. */
        if(object->length < 3){
            double D_X = 0;
            double D_Y = 0;

            foreach(record, object->records){
                D_X += record->sigma_x*record->sigma_x*sigma_correction*sigma_correction/object->length;
                D_Y += record->sigma_y*record->sigma_y*sigma_correction*sigma_correction/object->length;
            }

            gsl_matrix_scale(covX, D_X);
            gsl_matrix_scale(covY, D_Y);

            Dpos = D_X + D_Y;
        } else if((chisqX + chisqY)/(object->length - order - 1) < 2){
            gsl_matrix_scale(covX, chisqX/(object->length - order - 1));
            gsl_matrix_scale(covY, chisqY/(object->length - order - 1));

            Dpos = (chisqX + chisqY)/(object->length - order - 1);
        }
#else
        gsl_multifit_linear(T, X, CX, covX, &chisqX, work);
        gsl_multifit_linear(T, Y, CY, covY, &chisqY, work);
#endif

        /* Compute prediction position and size */
        {
            double time = 1e-3*time_interval(object->time0, time_current);
            gsl_vector *Tcurrent = gsl_vector_alloc(order + 1);
            double coeff = 1;
            double x = 0;
            double y = 0;
            double dx = 0;
            double dy = 0;

            for(i = 0; i <= order; i++){
                gsl_vector_set(Tcurrent, i, coeff);
                coeff *= time;

                /* ...and update object trajectory parameters */
                object->Cx[i] = gsl_vector_get(CX, i);
                object->Cy[i] = gsl_vector_get(CY, i);
            }

            gsl_multifit_linear_est(Tcurrent, CX, covX, &pr.x, &pr.dx);
            gsl_multifit_linear_est(Tcurrent, CY, covY, &pr.y, &pr.dy);

            if(object->length <= order + 1){
                pr.dx = 2;//initial_radius;
                pr.dy = 2;//initial_radius;
            }

            gsl_vector_set(Tcurrent, 0, 1);
            for(i = 1; i <= order; i++)
                gsl_vector_set(Tcurrent, i, 0);

            gsl_multifit_linear_est(Tcurrent, CX, covX, &x, &dx);
            gsl_multifit_linear_est(Tcurrent, CY, covY, &y, &dy);

            /* Whether predictions for 0 and time_current differ significantly */
            shift = sqrt((x - pr.x)*(x - pr.x) + (y - pr.y)*(y - pr.y));
            if((x - pr.x)*(x - pr.x) + (y - pr.y)*(y - pr.y) > 9*(Dpos + dx*dx + dy*dy))
                is_shift = TRUE;

            /* if(object->state == OBJECT_FLASH && is_shift) */
            /*     dprintf("pr = %g +/- %g, %g +/- %g\npr0 = %g +/- %g, %g +/- %g\n\n", */
            /*             pr.x, pr.dx, pr.y, pr.dy, x, dx, y, dy); */

            gsl_vector_free(Tcurrent);
        }

        /* Sanity check */
        pr.dx = MIN(pr.dx, initial_radius);
        pr.dy = MIN(pr.dy, initial_radius);

        if(object->length > 1){
            /* Ensure that prediction size is not larger than mean sigma of object' records. And not smaller than 1 px */
            double ms_x = 0;
            double ms_y = 0;
            int N = 0;

            foreach(record, object->records){
                ms_x += record->sigma_x;
                ms_y += record->sigma_y;
                N ++;
            }

            ms_x *= 1./N;
            ms_y *= 1./N;

            pr.dx = MAX(1.0, MIN(pr.dx, ms_x));
            pr.dy = MAX(1.0, MIN(pr.dx, ms_y));
        }


        /* /\* Simple slope-error-based motion check *\/ */
        /* if(object->Cx[1]*object->Cx[1]/gsl_matrix_get(covX, 1, 1) + */
        /*    object->Cy[1]*object->Cy[1]/gsl_matrix_get(covY, 1, 1) > 13.82) /\* 0.999 quantile for chisq2 with 2 dof *\/ */
        /*     pr.is_motion = TRUE; */

        /* Temporary */
        /* if(!is_shift) */
        /*     pr.is_motion = FALSE; */
        /* else */
        /*     pr.is_motion = TRUE; */

        /* Chi-squared test for motion */
        if(object->length > 2){
            double chi2 = 0;
            double mean_x = 0;
            double mean_y = 0;
            double sum_Nx = 0;
            double sum_Ny = 0;
            double p = 0;
            int N = 0;

            foreach(record, object->records){
                mean_x += record->x/record->sigma_x;
                mean_y += record->y/record->sigma_y;

                sum_Nx += 1.0/record->sigma_x;
                sum_Ny += 1.0/record->sigma_y;

                N += 1;
            }

            mean_x *= 1.0/sum_Nx;
            mean_y *= 1.0/sum_Ny;

            foreach(record, object->records){
                chi2 += pow((record->x - mean_x)/record->sigma_x/sigma_correction, 2);
                chi2 += pow((record->y - mean_y)/record->sigma_y/sigma_correction, 2);
            }

            p = 1-gsl_cdf_chisq_P(chi2, 2*N-2);

            /* dprintf("chi2 = %g, dof = %d, p=%g\n", chi2, 2*N-2, p); */

            if(p < 0.01)
                pr.is_motion = TRUE;
        }

        /* if(object->state == OBJECT_FLASH && object->length > 2 && pr.is_motion == TRUE){ */
        /*     dprintf("%Ld\n%g %g %g %g\n%g %g %g %g\n", object->id, */
        /*             object->Cx[1], sqrt(gsl_matrix_get(covX, 1, 1)), sqrt(gsl_matrix_get(covX, 1, 1)*chisqX/(object->length - order - 1)), chisqX, */
        /*             object->Cy[1], sqrt(gsl_matrix_get(covY, 1, 1)), sqrt(gsl_matrix_get(covY, 1, 1)*chisqY/(object->length - order - 1)), chisqY); */

        /*     foreach(record, object->records) */
        /*         dprintf("%g %g %g %g %g\n", 1e-3*time_interval(object->time0, record->time), record->x, record->y, record->sigma_x*sigma_correction, record->sigma_y*sigma_correction); */

        /*     dprintf("Prediction for %g is at %g +/- %g, %g +/- %g\n", */
        /*             1e-3*time_interval(object->time0, time_current), pr.x, pr.dx, pr.y, pr.dy); */

        /*     dprintf("Shift is %g, sigma is %g\n", shift, sqrt(Dpos)); */
        /*     //exit(1); */
        /* } */

        gsl_matrix_free(T);
        gsl_vector_free(X);
        gsl_vector_free(Y);
        gsl_vector_free(WX);
        gsl_vector_free(WY);
        gsl_vector_free(CX);
        gsl_vector_free(CY);
        gsl_matrix_free(covX);
        gsl_matrix_free(covY);

        gsl_multifit_linear_free (work);
    }

    return pr;
}

inline int object_prediction_check(prediction_str prediction, double x, double y, double dx, double dy, double threshold)
{
    if((fabs(prediction.x - x) <= threshold*sqrt(prediction.dx*prediction.dx + dx*dx)) &&
       (fabs(prediction.y - y) <= threshold*sqrt(prediction.dy*prediction.dy + dy*dy)))
        return TRUE;
    else
        return FALSE;
}

inline int object_prediction_check_record(prediction_str prediction, record_str *record, double threshold, double sigma_correction)
{
    int result = object_prediction_check(prediction, record->x, record->y, MAX(0, record->sigma_x*sigma_correction), MAX(0, record->sigma_y*sigma_correction), threshold);

    return result;
}

/* Check out-of-frame condition */
int object_prediction_is_out_of_frame(prediction_str prediction, average_image_str *avg)
{
    int result = FALSE;

    if(prediction.x + prediction.dx < 0 ||
       prediction.y + prediction.dy < 0 ||
       prediction.x - prediction.dx >= avg->width ||
       prediction.y - prediction.dy >= avg->height)
        result = TRUE;

    return result;
}

/* Size of prediction region */
inline double object_prediction_size(prediction_str prediction)
{
    double result = sqrt(prediction.dx*prediction.dx +
                         prediction.dy*prediction.dy);

    return result;
}

/* Distance of the record from prediction center */
inline double object_prediction_record_distance(prediction_str prediction, record_str *record)
{
    return sqrt((prediction.x - record->x)*(prediction.x - record->x) + (prediction.y - record->y)*(prediction.y - record->y));
}
