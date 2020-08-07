#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>

#include <gsl/gsl_multifit.h>

#include "utils.h"

#include "channel.h"

static void channel_calibrate_celostate(char *filename, double *a1_ptr, double *a2_ptr, double *b1_ptr, double *b2_ptr)
{
    FILE *file = fopen(filename, "r");
    double a1 = 0;
    double a2 = 0;
    double b1 = 0;
    double b2 = 0;

    if(file){
        double *pos = NULL;
        double *dra = NULL;
        double *ddec = NULL;
        int N = 0;
        int d;

        gsl_multifit_linear_workspace *work;
        gsl_matrix *X;
        gsl_vector *W;
        gsl_vector *Y1;
        gsl_vector *Y2;
        gsl_vector *C1;
        gsl_vector *C2;
        gsl_matrix *cov;

        double chisq = 0;

        while(!feof(file)){
            double shift = 0;
            double dist = 0;
            double ra = 0;
            double dec = 0;

            if(fscanf(file, "%lf %lf %lf %lf", &shift, &dist, &ra, &dec) == 4){
                pos = realloc(pos, sizeof(double)*(N+1));
                dra = realloc(dra, sizeof(double)*(N+1));
                ddec = realloc(ddec, sizeof(double)*(N+1));

                pos[N] = shift;
                dra[N] = ra;
                ddec[N] = dec;

                N ++;
            }
        }

        dprintf("%s: %d calibration points\n", filename, N);

        work = gsl_multifit_linear_alloc(N, 2);
        X = gsl_matrix_alloc(N, 2);
        W = gsl_vector_alloc(N);
        Y1 = gsl_vector_alloc(N);
        Y2 = gsl_vector_alloc(N);
        C1 = gsl_vector_alloc(2);
        C2 = gsl_vector_alloc(2);
        cov = gsl_matrix_alloc(2, 2);

        for(d = 0; d < N; d++){
            gsl_matrix_set(X, d, 0, 1.0);
            gsl_matrix_set(X, d, 1, pos[d]);
            gsl_vector_set(Y1, d, dra[d]);
            gsl_vector_set(Y2, d, ddec[d]);

            gsl_vector_set(W, d, exp(-pos[d]*pos[d]/(50*50))); /* Empirical weight to use mostly central part */
        }

        gsl_multifit_wlinear(X, W, Y1, C1, cov, &chisq, work);
        gsl_multifit_wlinear(X, W, Y2, C2, cov, &chisq, work);

        dprintf("%s: dRA = %g + %g * pos\n", filename, gsl_vector_get(C1, 0), gsl_vector_get(C1, 1));
        dprintf("%s: dDec = %g + %g * pos\n", filename, gsl_vector_get(C2, 0), gsl_vector_get(C2, 1));

        a1 = gsl_vector_get(C1, 1);
        a2 = gsl_vector_get(C2, 1);
        b1 = gsl_vector_get(C1, 0);
        b2 = gsl_vector_get(C2, 0);

        gsl_matrix_free(X);
        gsl_vector_free(W);
        gsl_vector_free(Y1);
        gsl_vector_free(Y2);
        gsl_vector_free(C1);
        gsl_vector_free(C2);
        gsl_matrix_free(cov);

        gsl_multifit_linear_free(work);
    }

    if(a1_ptr)
        *a1_ptr = a1;
    if(a2_ptr)
        *a2_ptr = a2;
    if(b1_ptr)
        *b1_ptr = b1;
    if(b2_ptr)
        *b2_ptr = b2;
}

void channel_calibrate_celostates(channel_str *channel, char *filename1, char *filename2)
{
    double a1 = 0;
    double b1 = 0;
    double c1 = 0;
    double a2 = 0;
    double b2 = 0;
    double c2 = 0;

    if(file_exists_and_normal(filename1) &&
       file_exists_and_normal(filename2)){
        double c11 = 0;
        double c21 = 0;
        double c12 = 0;
        double c22 = 0;

        dprintf("Celostates: reading calibration data\n");

        channel_calibrate_celostate(filename1, &a1, &a2, &c11, &c21);
        channel_calibrate_celostate(filename2, &b1, &b2, &c12, &c22);

        c1 = 0.5*(c11 + c12);
        c2 = 0.5*(c21 + c22);

        dprintf("Celostates: dRA = %g * pos1 + %g * pos2 + %g\n", a1, b1, c1);
        dprintf("Celostates: dDec = %g * pos1 + %g * pos2 + %g\n", a2, b2, c2);
    }

    channel->mirror0_dra = a1;
    channel->mirror0_ddec = a2;
    channel->mirror1_dra = b1;
    channel->mirror1_ddec = b2;
    channel->mirror_dra = c1;
    channel->mirror_ddec = c2;
}
