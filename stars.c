#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "utils.h"
#include "stars.h"
#include "db.h"
#include "coords.h"

star_str star_create()
{
    star_str star;

    init_list(star);

    return star;
}

star_str *star_create_ptr()
{
    star_str *star = (star_str *)malloc(sizeof(star_str));

    *star = star_create();

    return star;
}

star_str *star_copy(star_str *star)
{
    star_str *copy = star_create_ptr();

    memcpy(copy, star, sizeof(star_str));

    return copy;
}

void star_delete(star_str *star)
{
    if(star)
        free(star);
}

#ifndef DEGTORAD
#define DEGTORAD (M_PI/180.0)
#endif /* DEGTORAD */

void get_stars_list(struct list_head *stars, char *template, ...)
{
    va_list ap;
    char *buffer = NULL;
    db_str *db = db_create("dbname=favor2");
    //coords_str coords = coords_empty();

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    /* coords.ra0 = ra0; */
    /* coords.dec0 = dec0; */

    init_list(*stars);

    if(db){
        PGresult *res = db_query(db, buffer);
        int d;

        for(d = 0; d < PQntuples(res); d++){
            star_str *star = star_create_ptr();

            star->ra = db_get_double(res, d, 0);
            star->dec = db_get_double(res, d, 1);

            star->b = db_get_double(res, d, 2);
            star->v = db_get_double(res, d, 3);
            star->r = db_get_double(res, d, 4);

            /* Compute standard coordinates in advance */
            //coords_get_xi_eta_from_ra_dec(&coords, star->ra, star->dec, &star->xi, &star->eta);

            add_to_list(*stars, star);
        }

        PQclear(res);

        db_delete(db);
    }

    if(buffer)
        free(buffer);
}

void tycho2_get_stars_list(double ra0, double dec0, double sr0, int length, struct list_head *stars)
{
    get_stars_list(stars, "SELECT ra, dec, 0.76*bt+0.24*vt as b , 1.09*vt-0.09*bt as v, 0 as r FROM tycho2 WHERE "
                   "q3c_radial_query(ra, dec, %g, %g, %g) ORDER BY vt ASC LIMIT %d;",
                   ra0, dec0, sr0, length);

    dprintf("%d Tycho-2 stars extracted\n", list_length(stars));
}

void nomad_get_stars_list(double ra0, double dec0, double sr0, int length, struct list_head *stars)
{
    get_stars_list(stars, "SELECT ra, dec, b, v, r FROM nomad WHERE "
                   "q3c_radial_query(ra, dec, %g, %g, %g) ORDER BY v ASC LIMIT %d;",
                   ra0, dec0, sr0, length);

    dprintf("%d NOMAD stars extracted\n", list_length(stars));
}

void wbvr_get_stars_list(double ra0, double dec0, double sr0, int length, struct list_head *stars)
{
    get_stars_list(stars, "SELECT ra, dec, b, v, r FROM wbvr WHERE "
                   "q3c_radial_query(ra, dec, %g, %g, %g) ORDER BY v ASC LIMIT %d;",
                   ra0, dec0, sr0, length);

    dprintf("%d WBVR stars extracted\n", list_length(stars));
}

void pickles_get_stars_list(double ra0, double dec0, double sr0, int length, struct list_head *stars)
{
    get_stars_list(stars, "SELECT ra, dec, b, v, r FROM pickles WHERE "
                   "q3c_radial_query(ra, dec, %g, %g, %g) ORDER BY v ASC LIMIT %d;",
                   ra0, dec0, sr0, length);

    dprintf("%d Pickles stars extracted\n", list_length(stars));
}
