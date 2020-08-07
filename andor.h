#ifndef ANDOR_H
#define ANDOR_H

#include <atcore.h>
#include <glib.h>

#include "time_str.h"
#include "image.h"

typedef struct {
    AT_H handle;
    /* Reference point for timestamps */
    time_str time0;

    int is_simcam;

    /* Memory chunks */
    GHashTable *chunks;
} andor_str;

andor_str *andor_create();
void andor_delete(andor_str *);

void andor_info(andor_str *);

void andor_reset_clock(andor_str *);

void andor_acquisition_start(andor_str *);
void andor_acquisition_stop(andor_str *);
int andor_is_acquiring(andor_str *);
void andor_cool(andor_str *);

image_str *andor_get_image(andor_str *);
image_str *andor_wait_image(andor_str *, double );

AT_64 get_int(AT_H , const wchar_t *);
AT_BOOL get_bool(AT_H , const wchar_t *);
double get_float(AT_H , const wchar_t *);
AT_WC *get_string(AT_H , const wchar_t *);
int get_enum_index(AT_H , const wchar_t *);
AT_WC *get_enum_string(AT_H , const wchar_t *);

#endif /* ANDOR_H */
