#include <stdlib.h>
#include <math.h>

#include "utils.h"

#include "realtime.h"
#include "realtime_types.h"

void realtime_adjust_parameters(realtime_str *realtime)
{
    object_str *object = NULL;
    double sigma_correction = 0;
    double weights = 0;
    int Nsigmas = 0;

    foreach(object, realtime->objects){
        if((object->state == OBJECT_MOVING || object->state == OBJECT_FLASH) &&
           object->length > 10){
            record_str *record = NULL;

            foreach(record, object->records){
                double time = 1e-3*time_interval(object->time0, record->time);

                double calc_coord(double *C, double time)
                {
                    double value = 0;
                    double power = 1;
                    int i;

                    for(i = 0; i < OBJECT_MAXPARAMS; i++){
                        value += C[i]*power;
                        power *= time;
                    }

                    return value;
                }

                double delta_x = record->x - calc_coord(object->Cx, time);
                double delta_y = record->y - calc_coord(object->Cy, time);
                double delta = sqrt(delta_x*delta_x + delta_y*delta_y);
                double sigma = sqrt(record->sigma_x*record->sigma_x + record->sigma_y*record->sigma_y);

                /* For normal distrubution the mean value of delta is ~1.255 sigma */
                sigma_correction += record->excess*delta/1.255/sigma;
                weights += record->excess;
                Nsigmas ++;
            }
        }
    }

    if(Nsigmas){
        sigma_correction /= weights;

        dprintf("Sigma correction = %g (was %g)\n", sigma_correction, realtime->sigma_correction);

        if(sigma_correction > realtime->sigma_correction)
            realtime->sigma_correction += 0.03;
        else
            realtime->sigma_correction -= 0.03;
    }
}
