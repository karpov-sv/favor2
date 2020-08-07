#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils.h"

#include "records.h"

record_str record_create()
{
    record_str record;

    /* FIXME: direct initialization of all fields */
    memset(&record, 0, sizeof(record_str));

    init_list(record);

    return record;
}

record_str *record_create_ptr()
{
    record_str *record = (record_str *)malloc(sizeof(record_str));

    *record = record_create();

    return record;
}

void record_delete(record_str *record)
{
    free(record);
}

void record_extended_delete(record_extended_str *record)
{
    if(record->filter)
        free(record->filter);

    free(record);
}

record_str *record_copy(record_str *record_in)
{
    record_str *record = (record_str *)malloc(sizeof(record_str));

    memcpy(record, record_in, sizeof(record_str));

    return record;
}

void records_list_get_brightest_records(struct list_head *in, int length, struct list_head *out)
{
    int in_length = list_length(in);
    int out_length = MIN(length, in_length);
    record_str *records = (record_str *)malloc(sizeof(record_str)*in_length);
    record_str *record = NULL;
    int d = 0;

    foreach(record, *in)
        records[d++] = *record;

    int compare_records_flux(const void *v1, const void *v2)
    {
        double value = ((record_str *)v1)->flux - ((record_str *)v2)->flux;

        /* This way the brightest one will be the first */
        return value > 0 ? -1 : (value < 0 ? 1 : 0);
    }

    qsort(records, in_length, sizeof(record_str), compare_records_flux);

    init_list(*out);

    for(d = 0; d < out_length; d++){
        record_str *copy = record_copy(&records[d]);
        add_to_list_end(*out, copy);
    }

    free(records);
}
