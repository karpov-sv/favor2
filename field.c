#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <gsl/gsl_multifit.h>

#include "utils.h"
#include "field.h"
#include "records.h"
#include "match.h"
#include "stars.h"

field_str *field_create()
{
    field_str *field = (field_str *)malloc(sizeof(field_str));

    field->stars = NULL;

    field->coords = coords_empty();

    field->kd = kd_create(2);

    /* field->Cb = 0; */
    /* field->Cv = 0; */
    /* field->Cr = 0; */
    /* field->C = 0; */
    /* field->Csigma = 0; */

    field->filter = 0;

    field->catalogue = NULL;

    field->mag_min = 0;
    field->mag_max = 0;
    field->mag_limit = 0;

    field->verbose = FALSE;

    return field;
}

void field_delete(field_str *field)
{
    if(!field)
        return;

    if(field->stars)
        free(field->stars);

    if(field->kd)
        kd_free(field->kd);

    if(field->catalogue)
        free(field->catalogue);

    free(field);
}

void field_set_filter(field_str *field, char *filter)
{
    field->filter = FIELD_FILTER_CLEAR;

    if(filter){
        if(strcmp(filter, "B") == 0 ||
           strcmp(filter, "B+Pol") == 0)
            field->filter = FIELD_FILTER_B;

        if(strcmp(filter, "V") == 0 ||
           strcmp(filter, "V+Pol") == 0)
            field->filter = FIELD_FILTER_V;

        if(strcmp(filter, "R") == 0 ||
           strcmp(filter, "R+Pol") == 0)
            field->filter = FIELD_FILTER_R;
    }

    dprintf("Field filter is %d: %s\n", field->filter, filter);
}

void field_project_stars(field_str *field)
{
    int d;

    kd_clear(field->kd);

    for(d = 0; d < field->Nstars; d++){
        coords_get_xi_eta_from_ra_dec(&field->coords, field->stars[d].ra, field->stars[d].dec,
                                      &field->stars[d].xi, &field->stars[d].eta);

        kd_insert2(field->kd, field->stars[d].xi, field->stars[d].eta, &field->stars[d]);
    }
}

void field_set_stars(field_str *field, struct list_head *stars, char *catalogue)
{
    star_str *star = NULL;
    int d = 0;

    field->BminusV = 0;
    field->VminusR = 0;

    field->Nstars = list_length(stars);
    field->stars = realloc(field->stars, sizeof(star_str)*field->Nstars);

    kd_clear(field->kd);

    foreach(star, *stars){
        field->stars[d] = *star;
        field->stars[d].count = 0;
        field->stars[d].mag = 0;

        coords_get_xi_eta_from_ra_dec(&field->coords, field->stars[d].ra, field->stars[d].dec,
                                      &field->stars[d].xi, &field->stars[d].eta);

        field->BminusV += (field->stars[d].b - field->stars[d].v)/field->Nstars;
        field->VminusR += (field->stars[d].v - field->stars[d].r)/field->Nstars;

        d ++;
    }

    dprintf("Field has %d stars with |B-V|=%g and |V-R|=%g\n", field->Nstars, field->BminusV, field->VminusR);

    /* Sort the stars according to their brightness */
    int compare_star_brightness(const void *v1, const void *v2)
    {
        double value = ((star_str *)v1)->v - ((star_str *)v2)->v;

        /* This way brightest star will be the first */
        return (value > 0 ? 1 : (value < 0 ? -1 : 0));
    }

    qsort(field->stars, field->Nstars, sizeof(star_str), compare_star_brightness);

    /* Project local copies of stars to standard coordinates */
    field_project_stars(field);

    field->catalogue = make_string(catalogue);
}

void field_get_brightest_stars(field_str *field, int Nstars, struct list_head *stars)
{
    int N = MIN(Nstars, field->Nstars);
    int d;

    init_list(*stars);

    for(d = 0; d < N; d++){
        star_str *copy = star_copy(&field->stars[d]);

        add_to_list_end(*stars, copy);
    }
}

coords_str field_match_coords(field_str *field, struct list_head *objects)
{
    coords_str coords = coords_empty();
    struct list_head bstars;
    struct list_head bobjects;
    double sigma = 0;

    /* FIXME: set the number of stars for match somewhere else */
    field_get_brightest_stars(field, 100, &bstars);
    records_list_get_brightest_records(objects, 100, &bobjects);

    coords = match_coords_get_sigma(&bobjects, &bstars, field->coords.ra0, field->coords.dec0, &sigma);

    if(!coords_is_empty(&coords)){
        dprintf("Matched with pixel scale %g\"/pix and sigma %g\"\n",
                coords_get_pixscale(&coords)*3600, sigma*3600);

        field->coords = coords;
        field->max_dist = 3*sigma;

        /* Reproject local copies of stars to standard coordinates, as the cented just changed */
        field_project_stars(field);
    } else
        dprintf("Match failed\n");

    free_list(bstars);
    free_list(bobjects);

    return coords;
}

star_str *field_get_nearest_star(field_str *field, double xi, double eta)
{
    struct kdres *res = kd_nearest2(field->kd, xi, eta);
    /* FIXME: check for the number of results returned, as it may be zero */
    star_str *star = (star_str *)kd_res_item_data(res);

    kd_res_free(res);

    return star;
}

#if 0
star_str *field_get_nearest_similar_star(field_str *field, double xi, double eta, double mag)
{
    struct kdres *res = kd_nearest_range2(field->kd, xi, eta, field->max_dist);
    int Nresults = kd_res_size(res);
    star_str **stars_idx = (star_str **)malloc(sizeof(star_str *)*Nresults);
    star_str *star = NULL;
    int d = 0;

    /* Get all stars inside a given circle */
    while(!kd_res_end(res)){
        stars_idx[d++] = (star_str *)kd_res_item_data(res);
        kd_res_next(res);
    }

    /* Sort them by increasing distance from given point */
    int compare_star_dist(const void *v1, const void *v2)
    {
        star_str *star1 = *(star_str **)v1;
        star_str *star2 = *(star_str **)v2;
        double dist1 = sqrt(pow(star1->xi - xi, 2) + pow(star1->eta - eta, 2));
        double dist2 = sqrt(pow(star2->xi - xi, 2) + pow(star2->eta - eta, 2));

        return (dist1 > dist2 ? 1 : (dist1 < dist2 ? -1 : 0));
    }

    qsort(stars_idx, Nresults, sizeof(star_str *), compare_star_dist);

    /* Find the nearest one satisfying the conditions */
    for(d = 0; d < Nresults; d++){
        star_str *star1 = stars_idx[d];
        double delta_mag = mag - field->C - field->Cb*star1->b - field->Cv*star1->v - field->Cr*star1->r;

        if(abs(delta_mag) < 3*field->Csigma){
            star = star1;
            break;
        }
    }

    kd_res_free(res);

    return star;
}
#endif

/* First iteration - get all nearest pairs and estimate the mean errors of positions and colors  */
void field_calibrate(field_str *field, struct list_head *objects)
{
    int Nobjects = list_length(objects);
    record_str *object;
    FILE *fout = NULL;

    double *dists = (double *)malloc(sizeof(double)*Nobjects);
    double *Bs = (double *)malloc(sizeof(double)*Nobjects);
    double *Vs = (double *)malloc(sizeof(double)*Nobjects);
    double *Rs = (double *)malloc(sizeof(double)*Nobjects);
    double *mags = (double *)malloc(sizeof(double)*Nobjects);
    double *SNs = (double *)malloc(sizeof(double)*Nobjects);
    double *xs = (double *)malloc(sizeof(double)*Nobjects);
    double *ys = (double *)malloc(sizeof(double)*Nobjects);
    int *idx = (int *)malloc(sizeof(int)*Nobjects);
    int Ndists = 0;
    int Ngood = 0;

    dprintf("Calibrating: %d stars, %d objects\n", field->Nstars, Nobjects);

    foreach(object, *objects){
        double xi = coords_get_xi(&field->coords, object->x, object->y);
        double eta = coords_get_eta(&field->coords, object->x, object->y);
        star_str *star = field_get_nearest_star(field, xi, eta);
        double dist = sqrt(pow(star->xi - xi, 2) + pow(star->eta - eta, 2));

        /* Keep only good, non-elongated, non-truncated and non-saturated objects */
        if(!(object->flags & RECORD_TRUNCATED) && !(object->flags & RECORD_SATURATED) &&
           !(object->flags & RECORD_ELONGATED) && dist < field->max_dist){
            dists[Ndists] = dist;
            Bs[Ndists] = star->b;
            Vs[Ndists] = star->v;
            Rs[Ndists] = star->r;
            mags[Ndists] = -2.5*log10(object->flux);
            SNs[Ndists] = object->flux/object->flux_err;
            xs[Ndists] = object->x;
            ys[Ndists] = object->y;
            idx[Ndists] = Ndists;

            /* Debugging output for first iteration - distance versus magnitude */
            /* printf("%g %g %g %g %g\n", dists[Ndists], Bs[Ndists], Vs[Ndists], Rs[Ndists], mags[Ndists]); */

            Ndists ++;
        }
    }

    /* Sort stars according to distance from matched objects */
    int compare_star_dist_idx(const void *v1, const void *v2)
    {
        double dist1 = dists[*(int *)v1];
        double dist2 = dists[*(int *)v2];

        return (dist1 > dist2 ? 1 : (dist1 < dist2 ? -1 : 0));
    }

    qsort(idx, Ndists, sizeof(int), compare_star_dist_idx);

    Ngood = Ndists;

    if(Ngood < 10){
        dprintf("Too few (Ngood=%d) good objects on the frame!\n", Ngood);

        goto end;
    }

    /* Iteratively select only stars with dist < median + 3*sigma */
    while(1){
        double median = dists[idx[(int)floor(0.5*Ngood)]];
        double sum = 0;
        double sum2 = 0;
        double sigma = 0;
        int Ngood_prev = Ngood;
        int d;

        for(d = 0; d < Ngood; d++){
            sum += dists[idx[d]] - median;
            sum2 += pow(dists[idx[d]] - median, 2);
        }

        sigma = sqrt((sum2 - sum*sum/Ngood)/(Ngood - 1));

        /* Find the number of objects with dist < median + 3*sigma */
        int compare_is_between(const void *v1, const void *v2)
        {
            double limit = median + 3*sigma;
            double value = dists[*(int *)v2];
            double value_next = ((int *)v2 < idx + Ngood - 1) ? dists[*((int *)v2 + 1)] : value + 1;
            int result = (value < limit ? 1 : (value > limit ? -1 : 0));

            if(value < limit && value_next > limit)
                result = 0;

            return result;
        }

        /* Find the maximal value lower than the limit using non-local comparator */
        Ngood = (int *)bsearch(idx, idx, Ngood, sizeof(int), compare_is_between) - idx + 1;

        /* Convergence?.. */
        if(Ngood == 1 || Ngood == Ngood_prev)
            break;
    }

    /* Check whether this distance is significantly better than initial guess from coordinate match */
    if(dists[idx[Ngood - 1]] < 2*field->max_dist)
        field->max_dist = dists[idx[Ngood - 1]];
    else {
        /* Keep the old value, and adjust the Ngood correspondingly */
        Ngood = 0;
        while(dists[idx[Ngood]] < field->max_dist)
            Ngood ++;
    }

    dprintf("Max distance for match is %g\", %d stars\n", field->max_dist*3600, Ngood);

#if 0
    /* Determine the zero point for a current filter using linear model Cat = A*Instr + B */
    {
        photometry_str phot;
        double *B_good = malloc(sizeof(double)*Ngood);
        double *V_good = malloc(sizeof(double)*Ngood);
        double *R_good = malloc(sizeof(double)*Ngood);
        double *Instr_good = malloc(sizeof(double)*Ngood);
        double *x_good = malloc(sizeof(double)*Ngood);
        double *y_good = malloc(sizeof(double)*Ngood);
        int d;

        for(d = 0; d < Ngood; d++){
            B_good[d] = Bs[idx[d]];
            V_good[d] = Vs[idx[d]];
            R_good[d] = Rs[idx[d]];
            Instr_good[d] = mags[idx[d]];
            x_good[d] = xs[idx[d]];
            y_good[d] = ys[idx[d]];
        }

        phot = photometry_calibrate(B_good, V_good, R_good, Instr_good, x_good, y_good, Ngood, field->filter);

        free(B_good);
        free(V_good);
        free(R_good);
        free(Instr_good);
        free(x_good);
        free(y_good);
    }
#endif

    /* Photometric calbration */
    {
        int Nparams = 1;
        gsl_multifit_linear_workspace *work = gsl_multifit_linear_alloc(Ngood, Nparams);
        gsl_matrix *X = gsl_matrix_alloc(Ngood, Nparams);
        gsl_vector *Y = gsl_vector_alloc(Ngood);
        gsl_vector *W = gsl_vector_alloc(Ngood);
        gsl_vector *C = gsl_vector_alloc(Nparams);
        gsl_matrix *cov = gsl_matrix_alloc(Nparams, Nparams);
        double chisq = 0;
        double *cat = NULL;
        double mag_min = 100.0;//mags[idx[0]];
        double mag_max = -100.0;//mags[idx[0]];
        int d;

        if(field->filter == FIELD_FILTER_B)
            cat = Bs;
        else if(field->filter == FIELD_FILTER_V)
            cat = Vs;
        else if(field->filter == FIELD_FILTER_R)
            cat = Rs;
        else {
            field->filter = FIELD_FILTER_V;
            cat = Vs;
            dprintf("Unknown/clear filter! Assuming V filter for calibration.\n");
        }

        if(field->verbose)
            fout = fopen("out.field.instr.fit.txt", "w");

        for(d = 0; d < Ngood; d++){
            /* Weight is proportional to flux of the star in all bands */
            double weight = pow(10, -0.4*Bs[idx[d]]) + pow(10, -0.4*Vs[idx[d]]) + 0*pow(10, -0.4*Rs[idx[d]]);

            gsl_matrix_set(X, d, 0, 1.0);
            if(Nparams == 6){
                gsl_matrix_set(X, d, 1, xs[idx[d]]*xs[idx[d]]);
                gsl_matrix_set(X, d, 2, xs[idx[d]]);
                gsl_matrix_set(X, d, 3, xs[idx[d]]*ys[idx[d]]);
                gsl_matrix_set(X, d, 4, ys[idx[d]]);
                gsl_matrix_set(X, d, 5, ys[idx[d]]*ys[idx[d]]);
            }

            gsl_vector_set(Y, d, cat[idx[d]] - mags[idx[d]]);

            gsl_vector_set(W, d, weight);

            /* Debug output - instrumental versus catalogue mags */
            if(field->verbose)
                fprintf(fout, "%g %g %g %g %g \n", mags[idx[d]], cat[idx[d]], xs[idx[d]], ys[idx[d]], weight);
        }

        if(field->verbose)
            fclose(fout);

        gsl_multifit_linear(X, Y, C, cov, &chisq, work);
        /* gsl_multifit_wlinear(X, W, Y, C, cov, &chisq, work); */
        /* gsl_matrix_scale(cov, chisq/(Ngood - 1 /\* order *\/ - 1)); /\* This scaling is not needed for linear fit *\/ */

        /* dprintf("\nC = %g %g\n", gsl_vector_get(C, 0), gsl_vector_get(C, 1)); */
        /* dprintf("Cov = %g %g %g\n", gsl_matrix_get(cov, 0, 0), gsl_matrix_get(cov, 0, 1), gsl_matrix_get(cov, 1, 1)); */
        /* dprintf("chisq=%g\n\n", chisq); */

        field->coords.filter = field->filter;

        for(d = 0; d < 6; d++){
            /* dprintf("C[%d] = %g\n", d, gsl_vector_get(C, d)); */
            if(d < Nparams)
                field->coords.mag_C[d] = gsl_vector_get(C, d);
            else
                field->coords.mag_C[d] = 0;
        }

        if(field->verbose){
            fout = fopen("out.field.instr.mag.txt", "w");

            foreach(object, *objects){
                double instr = -2.5*log10(object->flux);
                double mag = coords_get_mag(&field->coords,  instr, object->x, object->y, NULL);

                fprintf(fout, "%g %g %g %g\n", instr, mag, object->x, object->y);
            }

            fclose(fout);
        }

        /* Minimal and maximal magnitudes of detected objects */
        foreach(object, *objects)
            if(!(object->flags & RECORD_TRUNCATED) && !(object->flags & RECORD_SATURATED)){
                double mag = coords_get_mag(&field->coords,  -2.5*log10(object->flux), object->x, object->y, NULL);

                mag_min = MIN(mag_min, mag);
                mag_max = MAX(mag_max, mag);
            }

        /* Let's estimate the photometric uncertainty now */
        {
            double sum = 0;
            double sum2 = 0;
            int d;

            for(d = 0; d < Ngood; d++){
                double res = cat[idx[d]] - coords_get_mag(&field->coords, mags[idx[d]], xs[idx[d]], ys[idx[d]], NULL);

                sum += res;
                sum2 += res*res;
            }

            field->coords.mag_sigma = sqrt((sum2 - sum*sum/Ngood)/(Ngood - 1));

            dprintf("Standard deviation is %g\n", field->coords.mag_sigma);
        }

        field->mag_min = mag_min;
        field->mag_max = mag_max;

        dprintf("mag_min = %g, mag_max = %g\n", field->mag_min, field->mag_max);

#if 0
        if(field->verbose)
            fout = fopen("out.field.cat.sn.txt", "w");

        /* Now fit the log(SN) as function of catalogue magnitude */
        for(d = 0; d < Ngood; d++){
            /* Weight is proportional to flux of the star in all bands */
            double weight = pow(10, -0.4*Bs[idx[d]]) + pow(10, -0.4*Vs[idx[d]]) + 0*pow(10, -0.4*Rs[idx[d]]);

            gsl_matrix_set(X, d, 0, 1.0);
            gsl_matrix_set(X, d, 1, cat[idx[d]]);

            gsl_vector_set(Y, d, log10(SNs[idx[d]]));

            gsl_vector_set(W, d, weight);

            if(field->verbose)
                fprintf(fout, "%g %g %g\n", cat[idx[d]], SNs[idx[d]], log10(SNs[idx[d]]));
        }

        if(field->verbose)
            fclose(fout);

        gsl_multifit_wlinear(X, W, Y, C, cov, &chisq, work);
        gsl_matrix_scale(cov, chisq/(Ngood - 1 /* order */ - 1)); /* This scaling is not needed for linear fit */

        {
            double a = gsl_vector_get(C, 1);
            double a_err = sqrt(gsl_matrix_get(cov, 1, 1));
            double b = gsl_vector_get(C, 0);
            double b_err = sqrt(gsl_matrix_get(cov, 0, 0));

            field->mag_limit = (log10(5) - b)/a;
            field->mag_limit_err = hypot(b_err/a, (log10(5) - b)*a_err/a/a);

            dprintf("limit (SN=5) = %g +/- %g\n", field->mag_limit, field->mag_limit_err);
        }

        gsl_matrix_free(X);
        gsl_vector_free(Y);
        gsl_vector_free(W);
        gsl_vector_free(C);
        gsl_matrix_free(cov);

        gsl_multifit_linear_free (work);
#endif
    }

end:

    free(dists);
    free(Bs);
    free(Vs);
    free(Rs);
    free(mags);
    free(SNs);
    free(xs);
    free(ys);
    free(idx);
}

/* Append various calibration information to FITS keywords */
void field_annotate_image(field_str *field, image_str *image)
{
    char *filters[] = {"Clear", "B", "V", "R"};
    if(!image)
        return;

    /* image_keyword_add_double(image, "PHOTO_C_0", field->C, "Instr = C_0 + C_B*B + C_V*V + C_R*R"); */
    /* image_keyword_add_double(image, "PHOTO_C_B", field->Cb, "Instr = C_0 + C_B*B + C_V*V + C_R*R"); */
    /* image_keyword_add_double(image, "PHOTO_C_V", field->Cv, "Instr = C_0 + C_B*B + C_V*V + C_R*R"); */
    /* image_keyword_add_double(image, "PHOTO_C_R", field->Cr, "Instr = C_0 + C_B*B + C_V*V + C_R*R"); */

    image_keyword_add(image, "PHOTO_FILTER", filters[field->filter], "Filter used for calibration");
    if(field->catalogue)
        image_keyword_add(image, "PHOTO_CATALOGUE", field->catalogue, "Catalogue used for calibration");

    /* image_keyword_add_double(image, "PHOTO_MAG_LIMIT", field->mag_limit, "Magnitude for SN=5 (extrapolated)"); */
    /* image_keyword_add_double(image, "PHOTO_MAG_LIMIT_ERR", field->mag_limit_err, "Magnitude for SN=5 (extrapolated)"); */
    /* image_keyword_add_double(image, "PHOTO_MAG_MIN", field->mag_min, "Minimal magnitude on the frame"); */
    /* image_keyword_add_double(image, "PHOTO_MAG_MAX", field->mag_max, "Maximal magnitude on the frame"); */

    image->coords = field->coords;
}

#if 0
/* Second pass - match taking into account photometric info and max distance */
void field_match_objects(field_str *field, struct list_head *objects)
{
    int Nobjects = list_length(objects);
    int Nmatched = 0;
    int Novermatched = 0;
    record_str *object;
    int d = 0;

    foreach(object, *objects){
        double xi = coords_get_xi(&field->coords, object->x, object->y);
        double eta = coords_get_eta(&field->coords, object->x, object->y);
        double mag = -2.5*log10(object->flux);
        star_str *star = field_get_nearest_similar_star(field, xi, eta, mag);

        if(star){
            /* Object has a matching star */
            /* FIXME: do something useful here */
            star->count ++;
            star->mag = mag;
            d++;
        } else {
            /* No matches for this object inside the acceptable parameter range */
        }
    }

    /* Dump all stars */
    for(d = 0; d < field->Nstars; d++){
        if(field->stars[d].count == 1)
            Nmatched ++;
        else if(field->stars[d].count > 1)
            Novermatched ++;

        printf("%g %g %g %g %d %g %g\n",
               field->stars[d].b, field->stars[d].v,
               field->C + field->Cb*field->stars[d].b + field->Cv*field->stars[d].v,
               field->stars[d].mag, field->stars[d].count,
               coords_get_x(&field->coords, field->stars[d].ra, field->stars[d].dec),
               coords_get_y(&field->coords, field->stars[d].ra, field->stars[d].dec));
    }

    dprintf("%d / %d stars matched, %d multimatches\n", Nmatched, field->Nstars, Novermatched);
}
#endif
