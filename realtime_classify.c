#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "realtime.h"
#include "realtime_types.h"
#include "realtime_predict.h"
#include "matrix.h"

static void realtime_handle_meteor(realtime_str *realtime, object_str *object)
{
    /* Meteor. We will select from new records the one with either
       closest angle (if elongated) or lying closest to the trail. */
    record_str *last = (record_str*)list_last_item(object->records);
    record_str *record = NULL;
    /* TODO: merge all meteor records for more accurate direction and position */
    double sin_theta = sin(last->theta);
    double cos_theta = cos(last->theta);
    record_str *closest_angle = NULL;
    record_str *closest_trail = NULL;
    double closest_delta_theta = 1e10;
    double closest_distance = 1e10;

    foreach(record, realtime->records){
        double dist = fabs(sin_theta*(record->x - last->x) - cos_theta*(record->y - last->y));

        if(record->flags & RECORD_ELONGATED /* && !(record->flags & RECORD_TRUNCATED) */){
            /* Elongated and not truncated record. Check angle */
            /* Truncation test commented out until we can reliably
               handle ordinary records here */
            double delta_theta = fabs(180./M_PI*acos(cos(record->theta)*cos_theta + sin(record->theta)*sin_theta));
            double maxdist2 = 3*(last->b*last->b + record->b*record->b);

            if(delta_theta < MIN(realtime->max_meteor_angle, closest_delta_theta) &&
               dist*dist < maxdist2){
                closest_delta_theta = delta_theta;
                closest_angle = record;
            }
        } else {
            /* Ordinary record. Check the distance from the trail. */
            /* FIXME: do not handle more than one ordinary records in a row, or
               else we may end catching non-moving flashes occured on the trail */
            /* double maxdist2 = last->b*last->b + record->a*record->a + record->b*record->b; */

            /* if(dist*dist < MIN(maxdist2, closest_distance)){ */
            /*     closest_distance = dist; */
            /*     closest_trail = record; */
            /* } */
        }
    }

    if(closest_angle)
        record = closest_angle;
    else if(closest_trail)
        record = closest_trail;
    else
        record = NULL;

    if(record){
        del_from_list(record);
        object_add_record(object, record);
    } else {
        /* No signs of the meteor on this frame. */
        record_str *record0 = (record_str *)list_first_item(object->records);

        if(object->length > 1 || record0->excess > 20)
            /* We hope to filter out most of noise events here */
            REALTIME_CALL_HOOK(realtime, object_finished_hook, object);

        object->state = OBJECT_FINISHED;
    }

}

void realtime_classify_records(realtime_str *realtime)
{
    object_str *object;
    record_str *record;
    int num_flashes = number_of_flashes(&realtime->objects);

    /* Find records belonging to already tracked objects */
    foreachback(object, realtime->objects){
        if(object->state == OBJECT_METEOR)
            /* Special handling of meteors */
            realtime_handle_meteor(realtime, object);
        else {
            record_str *record0 = list_first_item(object->records);

            if(object->state == OBJECT_SINGLE){
                /* On the first step (seeking second appearance of the object)
                   we create as many objects as needed to track all possible
                   trajectories */
                int has_new_records = FALSE;

                foreach(record, realtime->records){
                    double dist = sqrt((record0->x - record->x)*(record0->x - record->x) +
                                       (record0->y - record->y)*(record0->y - record->y));

                    if(!(record->flags & RECORD_ELONGATED) &&
                       dist < realtime->initial_catch_radius){
                        if(!has_new_records){
                            object->state = OBJECT_DOUBLE;

                            has_new_records = TRUE;

                            del_from_list_in_foreach_and_run(record, object_add_record(object, record));
                        } else {
                            /* We already used original object. Let's create new one */
                            object_str *new = object_create();
                            record_str *record0_copy = record_copy(record0);

                            new->state = OBJECT_DOUBLE;
                            new->id = realtime->object_counter ++;

                            /* Add to the start of object list, so it will not
                               appear in current foreach() cycle */
                            add_to_list(realtime->objects, new);

                            object_add_record(new, record0_copy);

                            del_from_list_in_foreach_and_run(record, object_add_record(new, record));
                        }
                    }
                }

                if(!has_new_records){
                    /* No appearance of the object on second frame */
                    record = list_first_item(object->records);

                    if(record && record->excess > 1e6 /* 100 */)
                        /* Single-frame but bright event - we should report it */
                        REALTIME_CALL_HOOK(realtime, object_finished_hook, object);

                    object->state = OBJECT_FINISHED;
                    /* del_from_list_in_foreach_and_run(object, object_delete(object)); */
                }
            } else {
                /* For the longer objects we'll select just one record - the
                   closest to the center of predicted position */
                int order = ((object->length < 10 || object->state == OBJECT_FLASH)? 1 : (object->length < 100 ? 2 : 3));
                prediction_str prediction = realtime_predict_weighted(object, realtime->time, order, realtime->initial_catch_radius, realtime->sigma_correction);
                record_str *closest_record = NULL;
                double closest_distance = 1e10;

                foreach(record, realtime->records){
                    double dist = object_prediction_record_distance(prediction, record);

                    if(!(record->flags & RECORD_ELONGATED) &&
                       object_prediction_check_record(prediction, record, 3, realtime->sigma_correction)){
                        if(dist < closest_distance){
                            closest_distance = dist;
                            closest_record = record;
                        }
                    } else
                        closest_distance = MIN(dist, closest_distance);
                }

                if(closest_record &&
                   /* We accept noise records only for moving objects */
                   /* (!(closest_record->flags & RECORD_NOISE) || object->state == OBJECT_MOVING) */TRUE){
                    del_from_list(closest_record);
                    object_add_record(object, closest_record);
                } else if(object->state == OBJECT_DOUBLE ||
                          (object->state == OBJECT_CHECKING && object->length <= 5) ||
                          (object->state == OBJECT_MOVING && object->length < 5)){
                    /* No detection of object on third frame (or moving - on fourth) */
                    object->state = OBJECT_FINISHED;
                } else if((object->state == OBJECT_MOVING && object_prediction_is_out_of_frame(prediction, realtime->avg)) ||
                          (object->state == OBJECT_MOVING && time_interval(object->time_last, realtime->time) > 1e3*realtime->moving_invisibility_interval) ||
                          (object->state == OBJECT_FLASH && time_interval(object->time_last, realtime->time) > 1e3*realtime->flash_invisibility_interval)){
                    /* Object faded or moved out of frame */
                    if(object->state != OBJECT_FLASH || object->is_reported)
                        REALTIME_CALL_HOOK(realtime, object_finished_hook, object);

                    object->state = OBJECT_FINISHED;
                } else {

                }

                /* Check object motion */
                if(object->state != OBJECT_FINISHED){
                    if(object->length > 3){
                        if(closest_record)
                            /* Update prediction taking into account just added record */
                            prediction = realtime_predict_weighted(object, realtime->time, order, realtime->initial_catch_radius, realtime->sigma_correction);

                        if(!prediction.is_motion){
                            if(time_interval(object->time0, realtime->time) > 1e3*realtime->flash_reporting_interval &&
                               !object->is_reported && !object_is_noise(object) && num_flashes <= 1){
                                /* Initial motion un-detection or cancellation */
                                REALTIME_CALL_HOOK(realtime, flash_detected_hook, object);
                                object->is_reported = TRUE;
                            }
                            object->state = OBJECT_FLASH;
                        } else if(object->state != OBJECT_MOVING){
                            if(object->state == OBJECT_FLASH && object->is_reported)
                                /* Detection of motion for a flash */
                                if(!object_is_noise(object))
                                    REALTIME_CALL_HOOK(realtime, flash_cancelled_hook, object);
                            object->state = OBJECT_MOVING;
                        }
                    } else
                        object->state = OBJECT_CHECKING;
                }
            }
        }
    }

    /* Dispatch unused (non-noise) records into new transient candidates */
    foreach(record, realtime->records){
        if(/* !(record->flags & RECORD_NOISE) */TRUE){
            object_str *new = object_create();

            new->id = realtime->object_counter ++;

            new->coords = realtime->coords;

            if(record->flags & RECORD_ELONGATED)
                new->state = OBJECT_METEOR;
            else
                new->state = OBJECT_SINGLE;

            add_to_list(realtime->objects, new);

            del_from_list_in_foreach_and_run(record, object_add_record(new, record));
        } else
            del_from_list_in_foreach_and_run(record, free(record));
    }
}

void realtime_cleanup_objects(realtime_str *realtime)
{
    object_str *object = NULL;

    foreach(object, realtime->objects){
        if(object->state == OBJECT_FINISHED){
            /* Do something useful */
            del_from_list_in_foreach_and_run(object, object_delete(object));
        } else if(object->state == OBJECT_MOVING && object->length > 10){
            /* Filter out very slow objects - stars with bad tracking mount */
            /* FIXME: we should fix it in some other way */
            if(fabs(object->Cx[1]) < 0.1 && fabs(object->Cy[1]) < 0.1) /* Slower than pixel per ten seconds */
               del_from_list_in_foreach_and_run(object, object_delete(object));
        }
    }
}
