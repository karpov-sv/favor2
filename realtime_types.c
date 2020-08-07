#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "realtime_types.h"

object_str *object_create()
{
    object_str *object = (object_str *)malloc(sizeof(object_str));
    int d;

    object->id = 0;

    object->coords = coords_empty();
    object->filter = NULL;

    object->length = 0;
    object->state = 0;

    object->time0 = time_zero();
    object->time_last = time_zero();

    for(d = 0; d < OBJECT_MAXPARAMS; d++){
        object->Cx[d] = 0;
        object->Cy[d] = 0;
    }

    object->length = 0;

    object->classification = NULL;
    object->is_reported = FALSE;

    init_list(object->records);

    return object;
}

void object_delete(object_str *object)
{
    if(!object)
        return;

    if(object->filter)
        free(object->filter);

    if(object->classification)
        free(object->classification);

    free_list(object->records);

    free(object);
}

void object_add_record(object_str *object, record_str *record)
{
    if(!object)
        return;

    add_to_list(object->records, record);

    if(!object->length)
        object->time0 = record->time;

    object->time_last = record->time;
    object->length ++;
}

object_str *object_copy(object_str *object)
{
    object_str *copy = object_create();
    record_str *record;

    copy->id = object->id;
    copy->time0 = object->time0;
    copy->time_last = object->time_last;

    copy->coords = object->coords;
    if(object->filter)
        copy->filter = make_string(object->filter);

    memcpy(copy->Cx, object->Cx, sizeof(double)*OBJECT_MAXPARAMS);
    memcpy(copy->Cy, object->Cy, sizeof(double)*OBJECT_MAXPARAMS);

    copy->state = object->state;

    copy->length = 0;

    if(object->classification)
        copy->classification = make_string(object->classification);

    init_list(copy->records);

    foreachback(record, object->records)
        object_add_record(copy, record_copy(record));

    return copy;
}

char *object_params_as_hstore(object_str *object)
{
    char *result = NULL;
    char *hstring = NULL;
    int d;

    add_to_string(&hstring, "\"classification\" => \"%s\"", object->classification ? object->classification : "");

    for(d = 0; d < OBJECT_MAXPARAMS; d++){
        add_to_string(&hstring, ", Cx%d => %g", d, object->Cx[d]);
        add_to_string(&hstring, ", Cy%d => %g", d, object->Cy[d]);
    }

    add_to_string(&hstring, ", length => %d", object->length);

    result = make_string("'%s'::hstore", hstring);

    free(hstring);

    return result;
}

double object_max_flux(object_str *object)
{
    double result = 0;
    record_str *record;

    foreach(record, object->records)
        result = MAX(result, record->flux);

    return result;
}

int object_is_noise(object_str *object)
{
    record_str *record;
    int result = FALSE;

    foreach(record, object->records)
        /* Conservative estimate - object is noise if any of its records is due to noise */
        if(record->flags & RECORD_NOISE){
            result = TRUE;
            break;
        }

    return result;
}

int number_of_flashes(struct list_head *objects)
{
    object_str *object;
    int N = 0;

    foreach(object, *objects)
        if(object->state == OBJECT_FLASH)
            N ++;

    return N;
}
