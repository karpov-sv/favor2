#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <fitsio2.h>

/* Suppress vast amount of debugging messages */
//#undef DEBUG

#include "utils.h"

#include "lists.h"
#include "coords.h"
#include "kdtree.h"
#include "records.h"
#include "stars.h"
#include "match.h"
#include "sextractor.h"

int wcs_match_degree = 2;
int wcs_refine_degree = 3;

typedef struct {
    double Tx;
    double Ty;
    /* Indices of corresponding vertices */
    int idx[3];
} triangle_str;

typedef struct {
    /* Length */
    int Nobjects;
    int Nstars;

    /* Center coordinates */
    double ra0;
    double dec0;

    /* Chirality */
    int chirality;

    /* Vertices */
    double *x_o;
    double *y_o;
    double *x_s;
    double *y_s;

    /* Triangles */
    int Nobjects_triangles;
    int Nstars_triangles;

    triangle_str *objects_triangles;
    triangle_str *stars_triangles;

    /* Voting matrix */
    int *vote;

    /* Matched pairs */
    int Npairs;
    int *pairs_o;
    int *pairs_s;
    int *pairs_vote;

    /* Statistics */
    double dist_sigma;
} match_str;

match_str *match_create()
{
    match_str *match = (match_str *)malloc(sizeof(match_str));

    match->Nobjects = 0;
    match->Nstars = 0;

    match->chirality = 1;

    match->x_o = NULL;
    match->y_o = NULL;
    match->x_s = NULL;
    match->y_s = NULL;

    match->objects_triangles = NULL;
    match->stars_triangles = NULL;

    match->vote = NULL;

    match->Npairs = 0;
    match->pairs_o = NULL;
    match->pairs_s = NULL;
    match->pairs_vote = NULL;

    return match;
}

void match_delete(match_str *match)
{
    if(!match)
        return;

    if(match->x_o)
        free(match->x_o);
    if(match->y_o)
        free(match->y_o);
    if(match->x_s)
        free(match->x_s);
    if(match->y_s)
        free(match->y_s);

    if(match->objects_triangles)
        free(match->objects_triangles);
    if(match->stars_triangles)
        free(match->stars_triangles);

    if(match->vote)
        free(match->vote);

    if(match->pairs_o)
        free(match->pairs_o);
    if(match->pairs_s)
        free(match->pairs_s);
    if(match->pairs_vote)
        free(match->pairs_vote);

    free(match);
}

void match_set_data(match_str *match, struct list_head *objects, struct list_head *stars,
                    int chirality, double ra0, double dec0)
{
    coords_str coords_center = coords_empty();
    record_str *object = NULL;
    star_str *star = NULL;
    int d;

    match->ra0 = ra0;
    match->dec0 = dec0;

    coords_center.ra0 = match->ra0;
    coords_center.dec0 = match->dec0;

    match->Nobjects = list_length(objects);
    match->Nstars = list_length(stars);

    match->chirality = chirality;

    match->x_o = realloc(match->x_o, sizeof(double)*match->Nobjects);
    match->y_o = realloc(match->y_o, sizeof(double)*match->Nobjects);
    match->x_s = realloc(match->x_s, sizeof(double)*match->Nstars);
    match->y_s = realloc(match->y_s, sizeof(double)*match->Nstars);

    /* Fill the vertex arrays */
    d = 0;

    foreach(object, *objects){
        match->x_o[d] = object->x;
        match->y_o[d] = object->y;
        d ++;
    }

    d = 0;
    foreach(star, *stars){
        coords_get_xi_eta_from_ra_dec(&coords_center, star->ra, star->dec, &star->xi, &star->eta);

        match->x_s[d] = star->xi*match->chirality;
        match->y_s[d] = star->eta;
        d ++;
    }
}

/* Build triangles for a given set of vertices (of length N) and the pre-computed distance matrix */
static int build_triangles(triangle_str **triangles_ptr, double *xs, double *ys, int N)
{
    int Ntriangles = 0;
    triangle_str *tri = (triangle_str *)malloc(sizeof(triangle_str)*N*(N-1)*(N-2)/6);
    double *dists = (double *)malloc(sizeof(double)*N*N);
    double max_dist = 0;
    double min_dist = 0;
    int i;
    int j;
    int k;

    dprintf("Computing distance matrix\n");

    for(i = 0; i < N; i++)
        for(j = i + 1; j < N; j++){
            double dx = xs[i] - xs[j];
            double dy = ys[i] - ys[j];
            double dist = sqrt(dx*dx + dy*dy);

            dists[i*N + j] = dist;
            dists[j*N + i] = dist;

            if(!min_dist)
                min_dist = dist;
            max_dist = MAX(dist, max_dist);
            min_dist = MIN(dist, min_dist);
        }

    dprintf("Expecting max %d triangles from %d points\n", N*(N-1)*(N-2)/6, N);

    inline double DIST(int i, int j) { return dists[i*N + j]; }

    for(i = 0; i < N - 2; i++)
        for(j = i + 1; j < N - 1; j++)
            for(k = j + 1; k < N; k++){
                triangle_str *t = &tri[Ntriangles];
                int id[3] = {0, 1, 2};
                int idx[3] = {i, j, k};
                double x[3] = {xs[i], xs[j], xs[k]};
                double y[3] = {ys[i], ys[j], ys[k]};
                double x0 = (x[0] + x[1] + x[2])/3;
                double y0 = (y[0] + y[1] + y[2])/3;
                double PA[3] = {atan2(y[0] - y0, x[0] - x0), atan2(y[1] - y0, x[1] - x0), atan2(y[2] - y0, x[2] - x0)};
                double max_side = MAX(DIST(i, j), MAX(DIST(i, k), DIST(j, k)));

                int compare_id(const void *i1, const void *i2){
                    double v = PA[*(int*)i1] - PA[*(int*)i2];

                    return (v > 0 ? 1 : (v < 0 ? -1 : 0));
                }

                /* Basic triangle filtering */
                if(max_side > 0.5*(max_dist + min_dist))
                    continue;

                /* Sort by position angle */
                qsort(id, 3, sizeof(int), compare_id);

                /* Rotate until 0-1 is the longest side */
                while(DIST(idx[id[0]], idx[id[1]]) != max_side){
                    int tmp = id[2];

                    id[2] = id[1];
                    id[1] = id[0];
                    id[0] = tmp;
                }

                {
                    /* Triangle coordinates in chiral triangle space, see Pal (2009) */
                    double alpha = 1 - DIST(idx[id[1]], idx[id[2]])/DIST(idx[id[0]], idx[id[1]]);
                    double beta = 1 - DIST(idx[id[0]], idx[id[2]])/DIST(idx[id[0]], idx[id[1]]);

                    double x1 = alpha*(alpha + beta)/sqrt(alpha*alpha + beta*beta);
                    double y1 = beta*(alpha + beta)/sqrt(alpha*alpha + beta*beta);
                    double x2 = x1*x1 - y1*y1;
                    double y2 = 2*x1*y1;
                    int d;

                    t->Tx = (x2*x2 - y2*y2)/pow(alpha + beta, 3);
                    t->Ty = 2*x2*y2/pow(alpha + beta, 3);

                    /* Store the ordered vertex indices */
                    for(d = 0; d < 3; d++)
                        t->idx[d] = idx[id[d]];
                }

                Ntriangles ++;
            }

    tri = realloc(tri, sizeof(triangle_str)*Ntriangles);

    if(*triangles_ptr)
        free(*triangles_ptr);

    *triangles_ptr = tri;

    dprintf("Got %d triangles\n", Ntriangles);

    free(dists);

    return Ntriangles;
}

static void __attribute__ ((unused)) dump_vote_matrix(match_str *match)
{
    int min = match->Nstars + match->Nobjects;
    int max = 0;
    int i;
    int j;

    for(i = 0; i < match->Nobjects*match->Nstars; i++){
        min = MIN(min, match->vote[i]);
        max = MAX(max, match->vote[i]);
    }

    dprintf("min votes = %d, max votes = %d\n", min, max);

    for(i = 0; i < match->Nobjects; i++){
        for(j = 0; j < match->Nstars; j++){
            dprintf("%3d ", match->vote[i*match->Nstars + j]);
        }
        dprintf("\n");
    }

}

static int match_triangles(match_str *match)
{
    /* FIXME: find some better way for estimating the triangle match ragius */
    double max_acceptable_dist = sqrt(1./match->Nstars)/10;
    int i;
    int nmatched = 0;

    match->vote = realloc(match->vote, sizeof(int)*match->Nobjects*match->Nstars);

    memset(match->vote, 0, sizeof(int)*match->Nobjects*match->Nstars);

    dprintf("Matching triangles with max distance %g\n", max_acceptable_dist);

    /* Cycle over all possible triangles */
#if 0
    /* FIXME: very inefficient. Some sort of binary search will help a lot */
    for(i = 0; i < match->Nobjects_triangles; i++){
        int min_j = -1;
        double min_dist = 2;
        int j;
        int d;

        for(j = 0; j < match->Nstars_triangles; j++){
            double dist = sqrt(pow(match->objects_triangles[i].Tx - match->stars_triangles[j].Tx, 2) +
                               pow(match->objects_triangles[i].Ty - match->stars_triangles[j].Ty, 2));
            if(dist < min_dist){
                min_dist = dist;
                min_j = j;
            }
        }

        /* Vote for all pairs of vertices in these triangles */
        if(min_dist < max_acceptable_dist){
            for(d = 0; d < 3; d++)
                match->vote[match->objects_triangles[i].idx[d]*match->Nstars + match->stars_triangles[min_j].idx[d]] ++;

            nmatched ++;
        }
    }
#else
    {
        /* Fast kd-tree based search, should be O(N*ln(N)) */
        struct kdtree *kd = kd_create(2);
        int *star_triangle_idx = (int *)malloc(sizeof(int)*match->Nstars_triangles);
        int d;

        for(i = 0; i < match->Nstars_triangles; i++){
            star_triangle_idx[i] = i;
            kd_insert2(kd, match->stars_triangles[i].Tx, match->stars_triangles[i].Ty, &star_triangle_idx[i]);
        }

        for(i = 0; i < match->Nobjects_triangles; i++){
            struct kdres *res = kd_nearest2(kd, match->objects_triangles[i].Tx, match->objects_triangles[i].Ty);
            /* FIXME: It should always returns at least one result, but beware corruption!.. */
            int j = *(int*)(kd_res_item_data(res));
            double dist = sqrt(pow(match->objects_triangles[i].Tx - match->stars_triangles[j].Tx, 2) +
                               pow(match->objects_triangles[i].Ty - match->stars_triangles[j].Ty, 2));

            /* Vote for all pairs of vertices in these triangles */
            if(dist < max_acceptable_dist){
                for(d = 0; d < 3; d++)
                    match->vote[match->objects_triangles[i].idx[d]*match->Nstars + match->stars_triangles[j].idx[d]] ++;

                nmatched ++;
            }

            kd_res_free(res);
        }

        free(star_triangle_idx);
        kd_free(kd);
    }
#endif

    dprintf("%d triangles matched in total\n", nmatched);

    /* dprintf("Raw vote matrix:\n"); */
    /* dump_vote_matrix(match); */

    /* Perform differential vote filtering, see Marszalek&Rokita (2006)
       Also, store the "good" pairs for the analysis */
    {
        int i;
        int j;
        int *vote_copy = (int *)malloc(sizeof(int)*match->Nobjects*match->Nstars);

        memcpy(vote_copy, match->vote, sizeof(int)*match->Nobjects*match->Nstars);

        /* Max possible number of pairs is MIN(Nobjects, Nstars),
           obviously. But we use MAX, just in case. */
        match->pairs_o = realloc(match->pairs_o, sizeof(int)*MAX(match->Nobjects, match->Nstars));
        match->pairs_s = realloc(match->pairs_s, sizeof(int)*MAX(match->Nobjects, match->Nstars));
        match->pairs_vote = realloc(match->pairs_vote, sizeof(int)*MAX(match->Nobjects, match->Nstars));

        match->Npairs = 0;

        for(i = 0; i < match->Nobjects; i++){
            for(j = 0; j < match->Nstars; j++){
                int value = vote_copy[i*match->Nstars + j];
                int base = 0;
                int k;

                for(k = 0; k < match->Nobjects; k++)
                    if(k != i)
                        base = MAX(base, vote_copy[k*match->Nstars + j]);
                for(k = 0; k < match->Nstars; k++)
                    if(k != j)
                        base = MAX(base, vote_copy[i*match->Nstars + k]);

                /* FIXME: invent some better estimation of value significance */
                if(value > base*2)
                    value -= base;
                else
                    value = 0;

                if(value){
                    match->pairs_o[match->Npairs] = i;
                    match->pairs_s[match->Npairs] = j;
                    match->pairs_vote[match->Npairs] = value;

                    match->Npairs ++;
                }

                match->vote[i*match->Nstars + j] = value;
            }
        }

        dprintf("%d stars matched reliably\n", match->Npairs);

        /* Shrink the pair arrays */
        match->pairs_o = realloc(match->pairs_o, sizeof(int)*match->Npairs);
        match->pairs_s = realloc(match->pairs_s, sizeof(int)*match->Npairs);
        match->pairs_vote = realloc(match->pairs_vote, sizeof(int)*match->Npairs);

        free(vote_copy);
    }

    /* dprintf("Differential vote matrix:\n"); */
    /* dump_vote_matrix(match); */

    return match->Npairs;
}

static coords_str get_transformation(match_str *match)
{
    coords_str coords = coords_empty();
    /* Assume rigid rotation, isotropic scaling, and shift. Then
       xi = a + b*x + c*y, eta = d - c*x + b*y */
    int N = match->Npairs*(match->Npairs - 1)/2;
    double *A = (double *)malloc(N*sizeof(double));
    double *B = (double *)malloc(N*sizeof(double));
    double *C = (double *)malloc(N*sizeof(double));
    double *D = (double *)malloc(N*sizeof(double));
    int d = 0;
    int i;
    int j;

    dprintf("Computing the transformation coefficients\n");

    for(i = 0; i < match->Npairs - 1; i++)
        for(j = i + 1; j < match->Npairs; j++){
            double x1 = match->x_o[match->pairs_o[i]];
            double x2 = match->x_o[match->pairs_o[j]];
            double y1 = match->y_o[match->pairs_o[i]];
            double y2 = match->y_o[match->pairs_o[j]];
            double xi1 = match->x_s[match->pairs_s[i]];
            double xi2 = match->x_s[match->pairs_s[j]];
            double eta1 = match->y_s[match->pairs_s[i]];
            double eta2 = match->y_s[match->pairs_s[j]];

            /* Solved in Maple, I did not check it */
            A[d] = (-xi1*x1*x2 + xi1*x2*x2 - y1*y2*xi1 + y2*y2*xi1 + xi2*x1*x1 - y2*x1*eta2 +
                    y2*eta1*x1 - xi2*x1*x2 + y1*y1*xi2 + y1*x2*eta2-y1*eta1*x2 - y1*y2*xi2) /
                (x1*x1 - 2*x1*x2 + x2*x2 + y1*y1 - 2*y1*y2+y2*y2);
            B[d] = (-x1*xi2 + xi1*x1 - xi1*x2 - y1*eta2 + y1*eta1 + y2*eta2 - y2*eta1 + xi2*x2) /
                (x1*x1 - 2*x1*x2 + x2*x2 + y1*y1 - 2*y1*y2 + y2*y2);
            C[d] = -(-x1*eta2 + eta1*x1 + y1*xi2 + x2*eta2 - eta1*x2 - y2*xi2 - y1*xi1 + y2*xi1) /
                (x1*x1 - 2*x1*x2 + x2*x2 + y1*y1 - 2*y1*y2 + y2*y2);
            D[d] = (-eta1*x1*x2 + eta1*x2*x2 - eta1*y1*y2 + eta1*y2*y2 + x1*x1*eta2 - x1*x2*eta2 +
                    x1*y2*xi2 - x1*y2*xi1 + y1*xi1*x2 + y1*y1*eta2 - y1*y2*eta2 - y1*xi2*x2) /
                (x1*x1 - 2*x1*x2 + x2*x2 + y1*y1 - 2*y1*y2 + y2*y2);

            d ++;
        }

    {
        double a = get_median(A, N);
        double b = get_median(B, N);
        double c = get_median(C, N);
        double d = get_median(D, N);

        //coords.CD10 = match->chirality*a;
        coords.CD11 = match->chirality*b;
        coords.CD12 = match->chirality*c;
        //coords.CD20 = d;
        coords.CD21 = -c;
        coords.CD22 = b;

        coords.ra0 = match->ra0;
        coords.dec0 = match->dec0;

        {
            double s_xi = 0;
            double s_xi2 = 0;
            double s_eta = 0;
            double s_eta2 = 0;
            int len = 0;
            int i;

            for(i = 0; i < match->Npairs; i++){
                double x = match->x_o[match->pairs_o[i]];
                double y = match->y_o[match->pairs_o[i]];
                double xi = match->x_s[match->pairs_s[i]];
                double eta = match->y_s[match->pairs_s[i]];

                double dxi = xi - (a + b*x + c*y);
                double deta = eta - (d - c*x + b*y);

                /* printf("%g %g %g %g %g %g\n", x, y, xi, eta, dxi, deta); */

                s_xi += dxi;
                s_eta += deta;
                s_xi2 += dxi*dxi;
                s_eta2 += deta*deta;
                len ++;
            }
            /* printf("\n"); */

            dprintf("dxi = %g +/- %g\n", s_xi/len*3600, sqrt((s_xi2 - s_xi*s_xi/len)/len/(len-1))*3600);
            dprintf("deta = %g +/- %g\n", s_eta/len*3600, sqrt((s_eta2 - s_eta*s_eta/len)/len/(len-1))*3600);

            match->dist_sigma = sqrt((s_xi2 - s_xi*s_xi/len)/(len-1) + (s_eta2 - s_eta*s_eta/len)/(len-1));
        }
    }

    //dprintf("pixel scale is %g\"/pix\n", sqrt(pow(coords.b, 2) + pow(coords.c, 2))*3600);

    free(A);
    free(B);
    free(C);
    free(D);

    return coords;
}

static int match_try(match_str *match, coords_str *coords_ptr)
{
    coords_str coords = coords_empty();
    int nmatches = 0;

    match->Nobjects_triangles = build_triangles(&match->objects_triangles, match->x_o, match->y_o, match->Nobjects);
    match->Nstars_triangles = build_triangles(&match->stars_triangles, match->x_s, match->y_s, match->Nstars);

    /* dump_points("out_o.txt", match->x_o, match->y_o, match->Nobjects); */
    /* dump_points("out_s.txt", match->x_s, match->y_s, match->Nstars); */

    /* dump_triangles("out_t_o.txt", match->objects_triangles, match->Nobjects_triangles); */
    /* dump_triangles("out_t_s.txt", match->stars_triangles, match->Nstars_triangles); */

    nmatches = match_triangles(match);

    if(nmatches > 1)
        /* Two points are enough for estimation of coordinate transformation */
        coords =  get_transformation(match);

    *coords_ptr = coords;

    return nmatches;
}

coords_str match_perform(match_str *match, struct list_head *objects, struct list_head *stars,
                         double ra0, double dec0)
{
    coords_str coords = coords_empty();
    coords_str coords_plus;
    coords_str coords_minus;
    int nmatches_plus = 0;
    int nmatches_minus = 0;
    int chirality = 0;

    /* Safety check */
    if(list_length(objects) < 10 || list_length(stars) < 10){
        dprintf("Not enough points for matching!\n");

        return coords;
    }

    /* Positive chirality */
    dprintf("Trying positive chirality\n");
    match_set_data(match, objects, stars, 1, ra0, dec0);
    nmatches_plus = match_try(match, &coords_plus);

    /* Negative chirality */
    dprintf("Trying negative chirality\n");
    match_set_data(match, objects, stars, -1, ra0, dec0);
    nmatches_minus = match_try(match, &coords_minus);

    if(nmatches_plus > nmatches_minus){
        dprintf("Accepting positive chirality\n");
        coords = coords_plus;
        chirality = 1;
    } else if(nmatches_plus < nmatches_minus){
        dprintf("Accepting negative chirality\n");
        coords = coords_minus;
        chirality = -1;
    } else
        dprintf("Match failed\n");

    if(chirality){
        /* Rough coordinates of the frame center */
        double x0 = get_median(match->x_o, match->Nobjects);
        double y0 = get_median(match->y_o, match->Nobjects);
        /* Projection on the sky */
        double ra = coords_get_ra(&coords, x0, y0);
        double dec = coords_get_dec(&coords, x0, y0);
        coords_str coords_refined = coords_empty();

        dprintf("Attempting the center refinement from (%g, %g) to (%g %g)\n", ra0, dec0, ra, dec);

        match_set_data(match, objects, stars, chirality, ra, dec);

        if(match_try(match, &coords_refined) > 0.5*MAX(nmatches_plus, nmatches_minus)){
            dprintf("Successful refinement from (%g, %g) to (%g %g)\n", ra0, dec0, ra, dec);
            coords = coords_refined;
        } else
            dprintf("Refinement failed\n");
    }

    return coords;
}

coords_str match_coords(struct list_head *objects, struct list_head *stars, double ra0, double dec0)
{
    match_str *match = match_create();
    coords_str coords = match_perform(match, objects, stars, ra0, dec0);

    match_delete(match);

    return coords;
}

coords_str match_coords_get_sigma(struct list_head *objects, struct list_head *stars, double ra0, double dec0, double *sigma_ptr)
{
    match_str *match = match_create();
    coords_str coords = match_perform(match, objects, stars, ra0, dec0);

    if(sigma_ptr)
        *sigma_ptr = match->dist_sigma;

    match_delete(match);

    return coords;
}

coords_str blind_match_coords(image_str *image)
{
    char *dirname = make_temp_dirname("/tmp/astrometry_XXXXXX");
    char *imagename = make_string("%s/image.fits", dirname);
    char *newname = make_string("%s/image.new", dirname);
    char *wcsname = make_string("%s/image.wcs", dirname);
    coords_str coords = coords_empty();
    char *command = NULL;

    /* Try to guess the path to Astrometry.Net binary */
    if(file_exists_and_normal("./astrometry/bin/solve-field"))
        command = "./astrometry/bin/solve-field";
    else if(file_exists_and_normal("/usr/local/astrometry/bin/solve-field"))
        command = "/usr/local/astrometry/bin/solve-field";
    else if(file_exists_and_normal("/opt/local/astrometry/bin/solve-field"))
        command = "/opt/local/astrometry/bin/solve-field";

    if(image && command){
        image_dump_to_fits(image, imagename);

        system_run("%s -t %d -D %s"
                   " --no-plots --no-fits2fits --overwrite --objs 300"
                   " --use-sextractor --fits-image %s --no-verify",
                   command, wcs_match_degree, dirname, imagename);

        if(file_exists_and_normal(newname)){
            /* Now let's verify the WCS we just matched */
            rename(newname, imagename);

            system_run("%s -t %d -D %s"
                       " --no-plots --no-fits2fits --overwrite --objs 3000"
                       " --use-sextractor --fits-image %s",
                       command, wcs_match_degree, dirname, imagename);
        }

        if(file_exists_and_normal(wcsname)){
            image_str *new = image_create_from_fits(wcsname);

            coords = new->coords;

            image_delete(new);
        } else
            dprintf("Image is not matched\n");
    }

    remove_dir(dirname);

    free(wcsname);
    free(newname);
    free(imagename);
    free(dirname);

    return coords;
}

void objects_dump_to_fits(struct list_head *objects, char *filename)
{
    fitsfile *fits;
    int status;
    char *filename_exclam = make_string("!%s", filename);
    char *tfields[] = {"XIMAGE", "YIMAGE", "FLUX"};
    char *ttype[] = {"1D", "1D", "1D"};
    record_str *object = NULL;
    int N = 0;

    if(!filename)
        return;

    cfitsio_lock();

    status = 0;
    fits_create_file(&fits, filename_exclam, &status);
    free(filename_exclam);

    fits_create_tbl(fits, BINARY_TBL, 0, 3, tfields, ttype, NULL, NULL, &status);

    foreach(object, *objects){
        /* FITS standard has (1, 1) as image corner, while we have (0, 0) */
        double x = object->x + 1;
        double y = object->y + 1;
        double flux = object->flux;

        N++;

        fits_write_col(fits, TDOUBLE, 1, N, 1, 1, &x, &status);
        fits_write_col(fits, TDOUBLE, 2, N, 1, 1, &y, &status);
        fits_write_col(fits, TDOUBLE, 3, N, 1, 1, &flux, &status);
    }

    fits_close_file(fits, &status);

    cfitsio_unlock();
}

coords_str blind_match_coords_from_list(struct list_head *objects, int width, int height)
{
    char *dirname = make_temp_dirname("/tmp/astrometry_XXXXXX");
    char *listname = make_string("%s/list.fits", dirname);
    char *tmpname = make_string("%s/list.tmp", dirname);
    char *wcsname = make_string("%s/list.wcs", dirname);
    coords_str coords = coords_empty();
    char *command = NULL;

    /* Try to guess the path to Astrometry.Net binary */
    if(file_exists_and_normal("./astrometry/bin/solve-field"))
        command = "./astrometry/bin/solve-field";
    else if(file_exists_and_normal("/usr/local/astrometry/bin/solve-field"))
        command = "/usr/local/astrometry/bin/solve-field";
    else if(file_exists_and_normal("/opt/local/astrometry/bin/solve-field"))
        command = "/opt/local/astrometry/bin/solve-field";

    if(command){
        objects_dump_to_fits(objects, listname);

        system_run("%s -t %d -D %s"
                   " --no-plots --no-fits2fits --overwrite --objs 300"
                   " --x-column XIMAGE --y-column YIMAGE --sort-column FLUX --width %d --height %d"
                   " %s",
                   command, wcs_match_degree, dirname, width, height, listname);

        if(file_exists_and_normal(wcsname)){
            rename(wcsname, tmpname);

            system_run("%s -t %d -D %s"
                       " --no-plots --no-fits2fits --overwrite --objs 3000"
                       " --x-column XIMAGE --y-column YIMAGE --sort-column FLUX --width %d --height %d"
                       " %s --verify %s",
                       command, wcs_match_degree, dirname, width, height, listname, tmpname);

        }

        if(file_exists_and_normal(wcsname)){
            image_str *new = image_create_from_fits(wcsname);

            coords = new->coords;

            image_delete(new);
        } else
            dprintf("Image is not matched\n");
    }

    remove_dir(dirname);

    free(wcsname);
    free(tmpname);
    free(listname);
    free(dirname);

    return coords;
}

int coords_refine_from_list(coords_str *coords, struct list_head *objects, struct list_head *stars, double pix_radius, int width, int height)
{
    char *filename = make_temp_filename("/tmp/matches_XXXXXX");
    char *wcsname = make_temp_filename("/tmp/wcs_XXXXXX");
    char *command = NULL;
    int result = FALSE;

    if(!list_length(objects) || !list_length(stars))
        return FALSE;

    /* Find matching stars and write them to temp file */
    {
        char *filename_exclam = make_string("!%s", filename);
        struct kdtree *kd = kd_create(2);
        char *tfields[] = {"X", "Y", "RA", "DEC"};
        char *ttype[] = {"1D", "1D", "1D", "1D"};
        fitsfile *fits = NULL;
        record_str *object;
        star_str *star;
        int N = 0;
        int status = 0;

        kd_clear(kd);

        foreach(star, *stars){
            coords_get_x_y(coords, star->ra, star->dec, &star->xi, &star->eta);
            kd_insert2(kd, star->xi, star->eta, star);
        }

        fits_create_file(&fits, filename_exclam, &status);

        fits_create_tbl(fits, BINARY_TBL, 0, 4, tfields, ttype, NULL, NULL, &status);

        foreach(object, *objects){
            struct kdres *res = NULL;

            res = kd_nearest_range2(kd, object->x, object->y, 1.0);
            /* Get all stars inside a given circle */
            while(!kd_res_end(res)){
                double x = object->x + 1;
                double y = object->y + 1;

                star = (star_str *)kd_res_item_data(res);

                N ++;

                fits_write_col(fits, TDOUBLE, 1, N, 1, 1, &x, &status);
                fits_write_col(fits, TDOUBLE, 2, N, 1, 1, &y, &status);
                fits_write_col(fits, TDOUBLE, 3, N, 1, 1, &star->ra, &status);
                fits_write_col(fits, TDOUBLE, 4, N, 1, 1, &star->dec, &status);

                kd_res_next(res);
            }

            kd_res_free(res);
        }

        fits_close_file(fits, &status);

        kd_free(kd);

        free(filename_exclam);
    }

    /* Try to guess the path to Astrometry.Net binary */
    if(file_exists_and_normal("./astrometry/bin/fit-wcs"))
        command = "./astrometry/bin/fit-wcs";
    else if(file_exists_and_normal("/usr/local/astrometry/bin/fit-wcs"))
        command = "/usr/local/astrometry/bin/fit-wcs";
    else if(file_exists_and_normal("/opt/local/astrometry/bin/fit-wcs"))
        command = "/opt/local/astrometry/bin/fit-wcs";

    if(command){
        system_run("%s -x %s -r %s -s %d -C -v -o %s", command, filename, filename, wcs_refine_degree, wcsname);

        if(file_exists_and_normal(wcsname)){
            image_str *image = image_create_from_fits(wcsname);

            if(image && !coords_is_empty(&image->coords)){
                *coords = image->coords;
                result = TRUE;
            }
        }
    } else
        dprintf("Can't find Astrometry.Net fit-wcs binary\n");

    if(file_exists_and_normal(filename))
        unlink(filename);
    if(file_exists_and_normal(wcsname))
        unlink(wcsname);

    free(filename);
    free(wcsname);

    return result;
}

void image_coords_refine(image_str *image, double pix_radius)
{
    struct list_head objects;
    struct list_head stars;
    double ra0;
    double dec0;

    sextractor_get_stars_list_aper(image, 100000, &objects, 10.0);

    coords_get_ra_dec(&image->coords, 0.5*image->width, 0.5*image->height, &ra0, &dec0);
    pickles_get_stars_list(ra0, dec0, 0.5*coords_distance(&image->coords, 0, 0, image->width, image->height), 100000, &stars);

    coords_refine_from_list(&image->coords, &objects, &stars, pix_radius, image->width, image->height);

    free_list(objects);
    free_list(stars);
}

coords_str blind_match_coords_from_list_and_refine(struct list_head *objects, double pix_radius, int width, int height)
{
    coords_str coords = blind_match_coords_from_list(objects, width, height);

    if(!coords_is_empty(&coords)){
        struct list_head stars;
        double ra0;
        double dec0;

        coords_get_ra_dec(&coords, 0.5*width, 0.5*height, &ra0, &dec0);
        pickles_get_stars_list(ra0, dec0, 0.5*coords_distance(&coords, 0, 0, width, height), 100000, &stars);

        coords_refine_from_list(&coords, objects, &stars, pix_radius, width, height);

        free_list(stars);
    }

    return coords;
}
