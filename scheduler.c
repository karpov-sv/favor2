#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

#include "utils.h"

#include "scheduler.h"
#include "coords.h"
#include "libastro/astro.h"
#include "db.h"

scheduler_str *scheduler_create()
{
    double step = 1.0;
    scheduler_str *sched = (scheduler_str *)malloc(sizeof(scheduler_str));
    double ps;
    int id;

    sched->nrings = (int)(180/step);
    sched->npixels0 = 2*sched->nrings;
    sched->npixels = (int *)malloc(sizeof(int)*sched->nrings);

    sched->dalpha = 30;
    sched->ddelta = 30;

    sched->latitude = 43.65722222;
    sched->longitude = -41.43235417;

    ps = cos(0.5*M_PI/sched->nrings)/sched->npixels0;
    for(id = 0; id < sched->nrings; id++)
        sched->npixels[id] = (int)(cos((id - sched->nrings/2 + 0.5)*M_PI/sched->nrings)/ps);

    /* We'll allocate memory assuming rectangular grid, to speed up access */
    sched->coverage = (double *)malloc(sizeof(double)*sched->nrings*sched->npixels0);
    sched->smooth = (double *)malloc(sizeof(double)*sched->nrings*sched->npixels0);
    sched->rweight = (double *)malloc(sizeof(double)*sched->nrings*sched->npixels0);
    sched->iweight = (double *)malloc(sizeof(double)*sched->nrings*sched->npixels0);

    init_list(sched->interests);

    scheduler_clear(sched);

    sched->time = time_zero();

    sched->db = db_create("dbname=favor2");

    return sched;
}

void scheduler_delete(scheduler_str *sched)
{
    scheduler_interest_str *interest;

    if(!sched)
        return;

    foreach(interest, sched->interests){
        if(interest->name)
            free(interest->name);
    }

    free_list(sched->interests);

    db_delete(sched->db);

    free(sched->npixels);

    free(sched->coverage);
    free(sched->smooth);
    free(sched->rweight);
    free(sched->iweight);

    free(sched);
}

void scheduler_clear(scheduler_str *sched)
{
    int d;

    sched->time = time_zero();

    for(d = 0; d < sched->nrings*sched->npixels0; d++){
        sched->coverage[d] = 0;
        sched->smooth[d] = 0;
        sched->rweight[d] = 0;
        sched->iweight[d] = 0;
    }
}

void scheduler_clear_plane(scheduler_str *sched, enum scheduler_plane plane)
{
    double *data = NULL;
    int d;

    if(plane == PLANE_COVERAGE)
        data = sched->coverage;
    else if(plane == PLANE_SMOOTH)
        data = sched->smooth;
    else if(plane == PLANE_RESTRICTIONS)
        data = sched->rweight;
    else if(plane == PLANE_INTEREST)
        data = sched->iweight;

    for(d = 0; d < sched->nrings*sched->npixels0; d++)
        data[d] = 0;
}

static inline void idx2ang(scheduler_str* sched, int ia, int id, double *alpha, double *delta)
{
    *delta = 180.0*((abs(id % sched->nrings) + 0.5) / sched->nrings - 0.5);
    *alpha = 360.0*((double)abs(ia % sched->npixels[id])/sched->npixels[id]);
}

static inline void ang2idx(scheduler_str* sched, double alpha, double delta, int* ia, int* id)
{
    delta = fmod (delta + 90.0, 360.0);

    if(delta < 0){
        if(delta < -180){
            delta += 360;
        } else {
            delta *= -1;
        }
    } else if(delta > 180){
        delta = 360.0 - delta;
    }

    *id = (int)(delta/(180.0/sched->nrings));

    alpha = fmod(alpha, 360.0);
    alpha = alpha < 0 ? 360.0 + alpha : alpha;

    *ia = (int)(alpha/(360.0/sched->npixels[*id == sched->nrings ? 0 : *id]));
}

/* Relative projected coordinates */
static inline void xi_eta_from_ra_dec(double ra0, double dec0, double ra, double dec, double *xi_ptr, double *eta_ptr)
{
    double xi = 0;
    double eta = 0;

    double dec_center = dec0*M_PI/180;
    double delta_ra;
    double xx;
    double yy;

    if((ra < 10) && (ra0 > 350))
        delta_ra = (ra + 360) - ra0;
    else if ((ra > 350) && (ra0 < 10))
        delta_ra = (ra - 360) - ra0;
    else
        delta_ra = ra - ra0;

    delta_ra *= M_PI/180;

    xx = cos(dec*M_PI/180)*sin(delta_ra);
    yy = sin(dec_center)*sin(dec*M_PI/180) +
        cos(dec_center)*cos(dec*M_PI/180)*cos(delta_ra);
    xi = (xx/yy);

    xx = cos(dec_center)*sin(dec*M_PI/180) -
        sin(dec_center)*cos(dec*M_PI/180)*cos(delta_ra);
    eta = (xx/yy);

    /* For FITS WCS standard the xi/eta is in degrees, not radians */
    xi *= 180./M_PI;
    eta *= 180./M_PI;

    if(xi_ptr)
        *xi_ptr = xi;
    if(eta_ptr)
        *eta_ptr = eta;
}


static inline void get_affected_rings(scheduler_str* sched, double alpha, double delta, double radius,
                                      int *min_id, int *max_id)
{
    int cd, ca, cr;
    int bid; /* Lower index by delta*/
    int uid; /* Upper index by delta*/

    ang2idx(sched, alpha, delta, &ca, &cd);
    cr = (int)((radius)/(180.0/sched->nrings));

    /* Which rings should be updated?*/
    bid = cd - cr - 1;
    uid = cd + cr + 1;

    if(bid < 0 && uid <= sched->nrings){
        *min_id = 0;
        *max_id = uid > - bid ? uid : - bid;
    } else if(bid > 0 && uid > sched->nrings){
        *min_id = bid < 2*sched->nrings - uid ? bid : 2*sched->nrings - uid;
        *max_id = sched->nrings;
    } else if(bid < 0 && uid > sched->nrings){
        *min_id = 0;
        *max_id = sched->nrings;
    } else {
        *min_id = bid;
        *max_id = uid;
    }

    if(*max_id == sched->nrings){
        *max_id--;
    }
}

/* Basically an intersection of two circles with given radius to account for
   smoothing due to non-zero area of FOV */
/* void scheduler_add_circle(scheduler_str *sched, double alpha, double delta, double radius, double value) */
/* { */
/*     int min_id = -1; */
/*     int max_id = -1; */
/*     int id; */

/*     get_affected_rings(sched, alpha, delta, 2*radius, &min_id, &max_id); */

/*     for(id = min_id; id <= max_id; id++){ */
/*         int ia; */

/*         for(ia = 0; ia < sched->npixels[id]; ia++){ */
/*             double a; */
/*             double d; */
/*             double dist; */

/*             idx2ang(sched, ia, id, &a, &d); */
/*             dist = coords_sky_distance(alpha, delta, a, d); */

/*             if(dist < radius) */
/*                 sched->coverage[ia + id*sched->npixels0] += value; */

/*             if(dist < 2*radius){ */
/*                 double v = 2*radius*radius*acos(dist/2/radius) - dist*radius*sqrt(1-pow(dist/2/radius,2)); */

/*                 sched->smooth[ia + id*sched->npixels0] += value*v/M_PI/radius/radius; */
/*             } */
/*         } */
/*     } */
/* } */

/* "Smoothed" intersection of two rectangles - one given (da, dd), and one predefined (dalpha, ddelta) */
void scheduler_add_rectangle(scheduler_str *sched, enum scheduler_plane plane,
                             double alpha, double delta, double da, double dd, double value)
{
    int min_id = -1;
    int max_id = -1;
    int id;

    get_affected_rings(sched, alpha, delta, 0.5*dd + sched->ddelta, &min_id, &max_id);

    for(id = min_id; id <= max_id; id++){
        int ia;

        for(ia = 0; ia < sched->npixels[id]; ia++){
            double a;
            double d;
            double dist;
            double deltaa;
            double deltad;

            idx2ang(sched, ia, id, &a, &d);

            xi_eta_from_ra_dec(alpha, delta, a, d, &deltaa, &deltad);

            dist = coords_sky_distance(alpha, delta, a, d);

            if(plane == PLANE_COVERAGE)
                /* Actual coverage */
                if(dist < 90  && fabs(deltaa) < 0.5*da && fabs(deltad) < 0.5*dd)
                    sched->coverage[ia + id*sched->npixels0] += value;

            /* Smoothed coverage - intersection of two rectangles, one centered
               at (0, 0) with (da, dd) size, another at (deltaa, deltad) with
               size (dalpha, ddelta) */
            if(dist < 90 && fabs(deltaa) < 0.5*(da + sched->dalpha) && fabs(deltad) < 0.5*(dd + sched->ddelta)){
                double overlap_a = MAX(0, MIN(0.5*da, deltaa + 0.5*sched->dalpha) - MAX(-0.5*da, deltaa - 0.5*sched->dalpha));
                double overlap_d = MAX(0, MIN(0.5*dd, deltad + 0.5*sched->ddelta) - MAX(-0.5*dd, deltad - 0.5*sched->ddelta));
                /* Normalize the area to the one of smaller rectangle, to set
                   it to unity for complete coverage */
                double s = overlap_a*overlap_d/MIN(da*dd, sched->dalpha*sched->ddelta);

                /* Special case of a zero-area point */
                if(da*dd == 0)
                    s = 1;

                if(plane == PLANE_COVERAGE)
                    sched->smooth[ia + id*sched->npixels0] += s*value;
                else if(plane == PLANE_INTEREST){
                    /* Try to place the region of interest as close to the center as possible */
                    //s *= exp(-dist/sqrt(sched->dalpha*sched->dalpha + sched->ddelta*sched->ddelta));
                    if(dist > sqrt(da*da + dd*dd))
                        s = 0;

                    sched->iweight[ia + id*sched->npixels0] += s*value;
                }
            }
        }
    }
}

static void get_sun_position(time_str time, double *ra_ptr, double *dec_ptr)
{
    long double MJD = time_str_get_JD(time) - 2415020; /* Dublin JD */
    double ra;
    double dec;
    double lsn;
    double rsn;

    sunpos((double)MJD, &lsn, &rsn, NULL);
    ecl_eq((double)MJD, 0, lsn, &ra, &dec);

    if(ra_ptr)
        *ra_ptr = 180.0*ra/M_PI;
    if(dec_ptr)
        *dec_ptr = 180.0*dec/M_PI;
}

static void get_moon_position(time_str time, double *ra_ptr, double *dec_ptr)
{
    long double MJD = time_str_get_JD(time) - 2415020; /* Dublin JD */
    double ra;
    double dec;
    double lam;
    double bet;
    double rho;
    double msp;
    double mdp;

    moon((double)MJD, &lam, &bet, &rho, &msp, &mdp);
    ecl_eq((double)MJD, bet, lam, &ra, &dec);

    if(ra_ptr)
        *ra_ptr = 180.0*ra/M_PI;
    if(dec_ptr)
        *dec_ptr = 180.0*dec/M_PI;
}

static void get_zenith_position(time_str time, double lat, double lon, double *ra_ptr, double *dec_ptr)
{
    double ra = time_str_get_local_sidereal_time(time, lon);
    double dec = lat;

    if(ra_ptr)
        *ra_ptr = 15.0*ra;
    if(dec_ptr)
        *dec_ptr = dec;
}

/* Restriction function - zero inside r_abs, 1-exponent with r0 outside */
static inline double restrict_fn(double r, double r_abs, double r0)
{
    double value = 1;

    if(r < r_abs)
        value = 0;
    else
        value = 1 - exp(-(r-r_abs)/r0);

    return value;
}

/* Update the map of restrictions due to Moon, Sun and horizon */
void scheduler_update_restrictions(scheduler_str *sched, time_str time)
{
    int id;
    double s_ra;
    double s_dec;
    double m_ra;
    double m_dec;
    double z_ra;
    double z_dec;
    double r0 = 0.5*MAX(sched->dalpha, sched->ddelta); /* Radius of a FOV circumscribed circle */

    get_sun_position(time, &s_ra, &s_dec);
    get_moon_position(time, &m_ra, &m_dec);
    get_zenith_position(time, sched->latitude, sched->longitude, &z_ra, &z_dec);

    for(id = 0; id < sched->nrings; id++){
        int ia;

        for(ia = 0; ia < sched->npixels[id]; ia++){
            double ra = 0;
            double dec = 0;
            double s_dist;
            double m_dist;
            double z_dist;
            double s_value = 1;
            double m_value = 1;
            double z_value = 1;

            idx2ang(sched, ia, id, &ra, &dec);

            s_dist = coords_sky_distance(ra, dec, s_ra, s_dec);
            m_dist = coords_sky_distance(ra, dec, m_ra, m_dec);
            z_dist = coords_sky_distance(ra, dec, z_ra, z_dec);

            s_value = restrict_fn(s_dist, 20 + r0, 20);
            m_value = restrict_fn(m_dist, 10 + r0, 10);
            z_value = restrict_fn(180 - z_dist, 110 + r0, 10);

            sched->rweight[ia + id*sched->npixels0] = s_value*m_value*z_value;
        }
    }
}

static void get_min_max_coverage(scheduler_str *sched, double *min_ptr, double *max_ptr)
{
    double min = sched->smooth[0];
    double max = sched->smooth[0];
    int id;

    for(id = 0; id < sched->nrings; id++){
        int ia;

        for(ia = 0; ia < sched->npixels[id]; ia++){
            min = MIN(min, sched->smooth[ia + id*sched->npixels0]);
            max = MAX(max, sched->smooth[ia + id*sched->npixels0]);
        }
    }

    if(min_ptr)
        *min_ptr = min;
    if(max_ptr)
        *max_ptr = max;
}

void scheduler_dump(scheduler_str *sched, const char *filename)
{
    FILE *file = (!filename || filename[0] == '-') ? stdout : fopen(filename, "w");
    int id;
    double min_coverage = 0;
    double max_coverage = 0;

    get_min_max_coverage(sched, &min_coverage, &max_coverage);

    for(id = 0; id < sched->nrings; id++){
        int ia;

        for(ia = 0; ia < sched->npixels[id]; ia++){
            double alpha = 0;
            double delta = 0;
            double value = max_coverage - sched->smooth[ia + id*sched->npixels0];
            double interest = sched->iweight[ia + id*sched->npixels0];

            if(max_coverage > min_coverage)
                value = 0.5 + value/(max_coverage - min_coverage)/2;
            else
                value = 0.5;

            value += 0.5*interest;

            value *= sched->rweight[ia + id*sched->npixels0];

            idx2ang(sched, ia, id, &alpha, &delta);

            fprintf(file, "%d %d %g %g %g %g %g %g %g\n", ia, id, alpha, delta,
                    sched->coverage[ia + id*sched->npixels0],
                    sched->smooth[ia + id*sched->npixels0],
                    sched->rweight[ia + id*sched->npixels0],
                    sched->iweight[ia + id*sched->npixels0],
                    value);
        }
    }

    if(file != stdout)
        fclose(file);
}

/* Choose optimal position according to current maps */
void scheduler_choose_survey_position(scheduler_str *sched, double *ra_ptr, double *dec_ptr)
{
    int ia_max = 0;
    int id_max = 0;
    double max_value = -1;
    double min_coverage = 0;
    double max_coverage = 0;
    int id;

    get_min_max_coverage(sched, &min_coverage, &max_coverage);

    for(id = 0; id < sched->nrings; id++){
        int ia;

        for(ia = 0; ia < sched->npixels[id]; ia++){
            double value = max_coverage - sched->smooth[ia + id*sched->npixels0];
            double interest = sched->iweight[ia + id*sched->npixels0];

            if(max_coverage > min_coverage)
                value = 0.5 + value/(max_coverage - min_coverage)/2;
            else
                value = 0.5;

            value += 0.5*interest;

            value *= sched->rweight[ia + id*sched->npixels0];

            if(value > max_value){
                max_value = value;
                ia_max = ia;
                id_max = id;
            }
        }
    }

    if(max_value >= 0 && ra_ptr && dec_ptr)
        idx2ang(sched, ia_max, id_max, ra_ptr, dec_ptr);
}

void scheduler_distances_from_point(scheduler_str *sched, time_str time, double ra, double dec,
                                    double *dsun_ptr, double *dmoon_ptr, double *dzenith_ptr)
{
    double s_ra;
    double s_dec;
    double m_ra;
    double m_dec;
    double z_ra;
    double z_dec;

    get_sun_position(time, &s_ra, &s_dec);
    get_moon_position(time, &m_ra, &m_dec);
    get_zenith_position(time, sched->latitude, sched->longitude, &z_ra, &z_dec);

    if(dsun_ptr)
        *dsun_ptr = coords_sky_distance(s_ra, s_dec, ra, dec);
    if(dmoon_ptr)
        *dmoon_ptr = coords_sky_distance(m_ra, m_dec, ra, dec);
    if(dzenith_ptr)
        *dzenith_ptr = coords_sky_distance(z_ra, z_dec, ra, dec);
}

double scheduler_sun_zenith_angle(scheduler_str *sched, time_str time)
{
    double s_ra;
    double s_dec;
    double z_ra;
    double z_dec;

    get_sun_position(time, &s_ra, &s_dec);
    get_zenith_position(time, sched->latitude, sched->longitude, &z_ra, &z_dec);

    return coords_sky_distance(s_ra, s_dec, z_ra, z_dec);
}

double scheduler_moon_zenith_angle(scheduler_str *sched, time_str time)
{
    double m_ra;
    double m_dec;
    double z_ra;
    double z_dec;

    get_moon_position(time, &m_ra, &m_dec);
    get_zenith_position(time, sched->latitude, sched->longitude, &z_ra, &z_dec);

    return coords_sky_distance(m_ra, m_dec, z_ra, z_dec);
}

void scheduler_update_interests(scheduler_str *sched)
{
    scheduler_interest_str *interest;

    scheduler_clear_plane(sched, PLANE_INTEREST);

    foreach(interest, sched->interests){
        scheduler_add_rectangle(sched, PLANE_INTEREST, interest->ra, interest->dec, 2*interest->radius, 2*interest->radius, interest->weight);
    }
}

void scheduler_dump_to_db(scheduler_str *sched)
{
    char *night = time_str_get_evening_date(time_current());
    char *timestamp = time_str_get_date_time(sched->time);
    const char *param_values[] = {night, (char *)sched->coverage, (char *)sched->smooth, timestamp};
    int param_lengths[] = {0, sizeof(double)*sched->nrings*sched->npixels0, sizeof(double)*sched->nrings*sched->npixels0, 0};
    int param_formats[] = {0, 1, 1, 0};

    PGresult *res1 = PQexecParams(sched->db->conn, "UPDATE scheduler_coverage SET coverage = $2::bytea, smooth = $3::bytea, time = $4::timestamp WHERE night = $1::text;",
                                  4, NULL, param_values, param_lengths, param_formats, 0);
    PGresult *res2 = PQexecParams(sched->db->conn,
                                  "INSERT INTO scheduler_coverage (night, coverage, smooth, time) SELECT $1::text, $2::bytea, $3::bytea, $4::timestamp"
                                  " WHERE NOT EXISTS (SELECT 1 FROM scheduler_coverage WHERE night = $1::text);",
                                  4, NULL, param_values, param_lengths, param_formats, 0);


    PQclear(res1);
    PQclear(res2);

    free(timestamp);
    free(night);
}

void scheduler_restore_from_db(scheduler_str *sched, time_str time)
{
    char *night = time_str_get_evening_date(time);
    PGresult *res = db_query(sched->db, "SELECT coverage, smooth, time FROM scheduler_coverage WHERE night = '%s';", night);

    if(PQntuples(res) == 1){
        size_t length = 0;
        char *scoverage = PQgetvalue(res, 0, 0);
        unsigned char *coverage = PQunescapeBytea((const unsigned char *)scoverage, &length);
        char *ssmooth = PQgetvalue(res, 0, 1);
        unsigned char *smooth = PQunescapeBytea((const unsigned char *)ssmooth, &length);
        char *time = PQgetvalue(res, 0, 2);

        memcpy(sched->coverage, coverage, sizeof(double)*sched->nrings*sched->npixels0);
        memcpy(sched->smooth, smooth, sizeof(double)*sched->nrings*sched->npixels0);

        sched->time = time_str_from_date_time(time);

        dprintf("Restored scheduler coverage from %s\n", time_str_get_date_time_static(sched->time));

        PQfreemem(coverage);
        PQfreemem(smooth);
    } else {
        dprintf("No stored scheduler coverage found for night %s\n", night);
        scheduler_clear(sched);
    }

    PQclear(res);

    free(night);
}

void scheduler_update_coverage(scheduler_str *sched, time_str time)
{
    char *night = time_str_get_evening_date(time);
    PGresult *res = NULL;
    int d;

    /* First, try to read current night coverage from the DB; it will clear it
       if the night has changed. It will also update the last change time stamp. */
    scheduler_restore_from_db(sched, time);

    /* Now, request all the images from the same night taken after the time stamp */
    res = db_query(sched->db, "SELECT time, ra0, dec0, width, height, keywords->'PIXSCALE', keywords->'EXPOSURE'"
                   " FROM images WHERE night = '%s' AND time > '%s';",
                   night, time_str_get_date_time_static(sched->time));

    for(d = 0; d < PQntuples(res); d++){
        char *timestamp = db_get_char(res, d, 0);
        double ra0 = db_get_double(res, d, 1);
        double dec0 = db_get_double(res, d, 2);
        int width = db_get_int(res, d, 3);
        int height = db_get_int(res, d, 4);
        double pixscale = db_get_double(res, d, 5);
        double exposure = db_get_double(res, d, 6);

        dprintf("Adding coverage at %g %g, %g x %g, %g seconds at %s\n",
                ra0, dec0, 1.0*width*pixscale, 1.0*height*pixscale, exposure, timestamp);

        scheduler_add_rectangle(sched, PLANE_COVERAGE, ra0, dec0, width*pixscale, height*pixscale, exposure);

        sched->time = time_str_from_date_time(timestamp);
    }

    PQclear(res);

    free(night);
}

int scheduler_target_is_observable_now(scheduler_str *sched, time_str time, double ra, double dec, double min_altitude, double max_moon_altitude, double min_moon_distance)
{
    double zsun = scheduler_sun_zenith_angle(sched, time);
    double zmoon = scheduler_moon_zenith_angle(sched, time);
    double dmoon = 0;
    double dzenith = 0;

    scheduler_distances_from_point(sched, time, ra, dec, NULL, &dmoon, &dzenith);

    //dprintf("ra=%g dec=%g: zsun=%g zmoon=%g dmoon=%g dzenith=%g\n", ra, dec, zsun, zmoon, dmoon, dzenith);

    /* FIXME: This should be unified somehow as we use the same check in beholder.c */
    if(zsun > 90 + 10 &&
       zmoon > (90 - max_moon_altitude) &&
       dmoon > min_moon_distance &&
       dzenith < (90 - min_altitude))
        return TRUE;
    else
        return FALSE;
}

int scheduler_target_is_observable(scheduler_str *sched, time_str time0, double ra, double dec, double min_altitude, double max_moon_altitude, double min_moon_distance)
{
    time_str time = time0;
    int is_observable = FALSE;

    while(!is_observable && 1e-3*time_interval(time0, time) < 86400){
        if(scheduler_target_is_observable_now(sched, time, ra, dec, min_altitude, max_moon_altitude, min_moon_distance))
            is_observable = TRUE;

        time_increment(&time, 15*60);
    }

    return is_observable;
}

scheduler_target_str *scheduler_choose_target(scheduler_str *sched, time_str time)
{
    scheduler_target_str *so = NULL;
    PGresult *res = db_query(sched->db, "SELECT id, external_id, name, type, ra, dec, exposure, repeat, filter, uuid"
                             " FROM scheduler_targets WHERE status = get_scheduler_target_status_id('active');");
    int d;

    for(d = 0; d < PQntuples(res); d++){
        double ra = db_get_double(res, d, 4);
        double dec = db_get_double(res, d, 5);
        double min_altitude = 25;
        double max_moon_altitude = 90;
        double min_moon_distance = 30;

        /* Here should be some actual scheduling, but for now we will just select first observable object */
        if(scheduler_target_is_observable_now(sched, time, ra, dec, min_altitude, max_moon_altitude, min_moon_distance)){
            so = (scheduler_target_str *)malloc(sizeof(scheduler_target_str));

            so->id = db_get_int(res, d, 0);
            so->external_id = db_get_int(res, d, 1);
            so->name = db_get_char(res, d, 2);
            so->type = db_get_char(res, d, 3);
            so->ra = ra;
            so->dec = dec;
            so->exposure = db_get_double(res, d, 6);
            so->repeat = db_get_double(res, d, 7);
            so->filter = db_get_char(res, d, 8);
            so->uuid = db_get_char(res, d, 9);

            break;
        }
    }

    PQclear(res);

    return so;
}

void scheduler_target_delete(scheduler_target_str *so)
{
    if(!so)
        return;

    if(so->name)
        free(so->name);

    if(so->type)
        free(so->type);

    if(so->filter)
        free(so->filter);

    if(so->uuid)
        free(so->uuid);

    free(so);
}

void scheduler_target_create(scheduler_str *sched, char *name, char *type, double ra, double dec, double exposure, char *filter)
{
    /* We need to check the plan and accept or reject it */
    char *name_fixed = make_string(name);
    char *pos = name_fixed;

    while(pos && *pos){
        if(!isalnum(*pos) && *pos != '_' && *pos != '-' && *pos !='+')
            *pos = '_';

        pos ++;
    }

    db_query(sched->db, "INSERT INTO scheduler_targets (name, type, ra, dec, exposure, filter, status, time_created)"
             " VALUES ('%s', '%s', %g, %g, %g, '%s', get_scheduler_target_status_id('active'), favor2_timestamp());",
             name_fixed, type, ra, dec, exposure, filter);

    if(name_fixed)
        free(name_fixed);
}

void scheduler_target_activate(scheduler_str *sched, int id)
{
    PGresult *res = db_query(sched->db, "UPDATE scheduler_targets SET status = get_scheduler_target_status_id('active') WHERE id = %d;", id);

    PQclear(res);
}

void scheduler_target_deactivate(scheduler_str *sched, int id)
{
    PGresult *res = db_query(sched->db, "UPDATE scheduler_targets SET status = get_scheduler_target_status_id('inactive') WHERE id = %d;", id);

    PQclear(res);
}

void scheduler_target_complete(scheduler_str *sched, int id)
{
    PGresult *res = db_query(sched->db, "UPDATE scheduler_targets SET (status, time_completed) = (get_scheduler_target_status_id('completed'),  favor2_timestamp()) WHERE id = %d;", id);

    PQclear(res);
}

scheduler_interest_str *scheduler_find_interest(scheduler_str *sched, char *name)
{
    scheduler_interest_str *interest = NULL;

    foreach(interest, sched->interests)
        if(!strcmp(interest->name, name))
            return interest;

    return NULL;
}

scheduler_interest_str *scheduler_add_interest(scheduler_str *sched, char *name)
{
    scheduler_interest_str *interest = calloc(1, sizeof(scheduler_interest_str));

    add_to_list(sched->interests, interest);

    interest->name = make_string(name);
    interest->weight = 1;
    interest->is_reschedule = FALSE;

    return interest;
}

void scheduler_delete_interest(scheduler_str *sched, char *name)
{
    scheduler_interest_str *interest = NULL;

    foreach(interest, sched->interests)
        if(!strcmp(interest->name, name)){
            free_and_null(interest->name);
            del_from_list_in_foreach_and_run(interest, free(interest));
        }
}
