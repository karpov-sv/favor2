#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "scheduler.h"
#include "db.h"

int main(int argc, char **argv)
{
    scheduler_str *sched = scheduler_create();
    time_str time = time_current();
    time_str time0 = time_current();
    double r0 = 30;
    int d;
    int N = 0;

    //scheduler_add_rectangle(sched, PLANE_INTEREST, 50, 0, 80, 80, 1);
    //scheduler_add_rectangle(sched, PLANE_INTEREST, 250, -50, 100, 100, 1);

    /* scheduler_update_coverage(sched, time); */

    /* //exit(1); */

    /* for(d = 0; d < 0*24*3; d++){ */
    /*     scheduler_update_restrictions(sched, time); */
    /*     scheduler_update_interests(sched, time, 20*60); */

    /*     if(scheduler_sun_zenith_angle(sched, time) > 90+15){ */
    /*         char *timestamp = time_str_get_date_time(time); */
    /*         double ra = 0; */
    /*         double dec = 0; */
    /*         double dsun = 0; */
    /*         double dmoon = 0; */
    /*         double dzenith = 0; */

    /*         scheduler_choose_survey_position(sched, &ra, &dec); */

    /*         scheduler_distances_from_point(sched, time, ra, dec, &dsun, &dmoon, &dzenith); */

    /*         dprintf("%s: pointing to %g %g, sun %g moon %g zenith %g\n", */
    /*                 timestamp, ra, dec, dsun, dmoon, dzenith); */

    /*         printf("%f %g %g %g %g %g\n", 1e-3*time_interval(time0, time), ra, dec, dsun, dmoon, dzenith); */

    /*         scheduler_add_rectangle(sched, PLANE_COVERAGE, ra, dec, r0, r0, 1.0/3); */

    /*         sched->time = time; */

    /*         free(timestamp); */

    /*         N ++; */
    /*     } */

    /*     time_increment(&time, 20*60); */
    /* } */

    /* scheduler_dump_to_db(sched); */
    /* scheduler_restore_from_db(sched, time); */

    /* scheduler_dump(sched, "out"); */


    scheduler_target_str *so = scheduler_choose_target(sched, time_incremented(time_current(), -7600));

    if(so){
        dprintf("Object %d selected\n", so->id);

        scheduler_target_delete(so);
    }

    scheduler_delete(sched);

    return EXIT_SUCCESS;
}
