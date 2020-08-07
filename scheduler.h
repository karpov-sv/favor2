#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "time_str.h"
#include "db.h"
#include "lists.h"

/*
  The task of the scheduler is to select the optimal pointing on the sky for a
  wide-field system, the box of dalpha x ddelta degress in size, which is most
  optimal according to the set of constraints (horizon, Moon and Sun), corevage
  of the sky by previous observations (we wish to cover the sky uniformly) and
  provided set of regions of interest worth following preferably.

  The idea is to just find the maximum on some surface, which is
  computationally much faster than direct optimization of the large box
  position on the sky. To do so, we "smooth" all the elements - constraints,
  previous pointings, points or regions of interest - so that the surface value
  in a given point is proportional to the area of intersection of the FOV box
  in this point with the region we add to the surface

  The final surface (figure of merit) is constructed as follows:

  F = (W + 0.5*I)*R,

  where
  R is the map of restrictions (0 - strictly not observable, 1 - no restrictions),
  W = 0.5 + 0.5*(max(S) - S)/(max(S)-min(S)),
  S is a smoothed sky coverage in absolute units, and
  I is map of regions of interest, with TBD scaling.

  TODO:
  - decide how to scale I
  - decide whether to use S from the whole set or from the night in question only
*/

/*
  Scheduler extension - point objects.

*/

typedef struct {
    int nrings;
    int npixels0;
    int *npixels;

    /* Basic smoothing box - the size of FOV, assuming well-aligned camera on
       equatorial mount */
    double dalpha;
    double ddelta;

    /* Observational point */
    double latitude;
    double longitude;

    /* When the coverage has been updated */
    time_str time;

    /* DB connection */
    db_str *db;

    /* Current survey coverage of the sky */
    double *coverage;
    /* Smoothed coverage */
    double *smooth;
    /* Map of restrictions - zero is prohibition, 1 is no restrictions */
    double *rweight;
    /* Map of directions of interest */
    double *iweight;

    /* Regions of interest to operate at runtime, without DB support */
    struct list_head interests;
} scheduler_str;

enum scheduler_plane {
    PLANE_COVERAGE,
    PLANE_SMOOTH,
    PLANE_RESTRICTIONS,
    PLANE_INTEREST
};

typedef struct scheduler_interest_str {
    LIST_HEAD(struct scheduler_interest_str);

    char *name;

    double ra;
    double dec;
    double radius;

    /* Relative weight */
    double weight;

    /* Whether to reschedule on update */
    int is_reschedule;
} scheduler_interest_str;

typedef struct {
    int id;
    int external_id;

    char *name;
    char *type;

    double ra;
    double dec;
    double exposure;
    int repeat;
    char *filter;

    char *uuid;
} scheduler_target_str;

scheduler_str *scheduler_create();
void scheduler_delete(scheduler_str *);
void scheduler_clear(scheduler_str *);
void scheduler_clear_plane(scheduler_str *, enum scheduler_plane );
/* void scheduler_add_circle(scheduler_str *, double , double , double , double ); */
void scheduler_add_rectangle(scheduler_str *, enum scheduler_plane , double , double , double , double , double );
void scheduler_update_restrictions(scheduler_str *, time_str );
void scheduler_dump(scheduler_str *, const char *);
void scheduler_choose_survey_position(scheduler_str *, double *, double *);
void scheduler_distances_from_point(scheduler_str *, time_str , double , double , double *, double *, double *);
double scheduler_sun_zenith_angle(scheduler_str *, time_str );
double scheduler_moon_zenith_angle(scheduler_str *, time_str );

void scheduler_update_interests(scheduler_str *);
void scheduler_update_coverage(scheduler_str *, time_str );
void scheduler_dump_to_db(scheduler_str *);
void scheduler_restore_from_db(scheduler_str *, time_str );

scheduler_target_str *scheduler_choose_target(scheduler_str *, time_str );
void scheduler_target_delete(scheduler_target_str *);

void scheduler_target_create(scheduler_str *, char *, char *, double , double , double , char *);
void scheduler_target_activate(scheduler_str *, int );
void scheduler_target_deactivate(scheduler_str *, int );
void scheduler_target_complete(scheduler_str *, int );

int scheduler_target_is_observable_now(scheduler_str *, time_str , double , double , double , double , double );
int scheduler_target_is_observable(scheduler_str *, time_str , double , double , double , double , double );

scheduler_interest_str *scheduler_add_interest(scheduler_str *, char *);
scheduler_interest_str *scheduler_find_interest(scheduler_str *, char *);
void scheduler_delete_interest(scheduler_str *, char *);

#endif /* SCHEDULER_H */
