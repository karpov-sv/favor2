#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/stat.h>
#include <glib.h>

#include "utils.h"

#include "random.h"

#include "andor.h"

#include "dataserver.h"

static int is_acquiring = FALSE;

dataserver_str *server = NULL;

andor_str *andor_create()
{
    andor_str *andor = NULL;

    andor = (andor_str *)malloc(sizeof(andor_str));

    andor->time0 = time_current();

    andor->is_simcam = FALSE;

    is_acquiring = FALSE;

    server = dataserver_create("IMG");

    dprintf("Fake ANDOR created\n");

    return andor;
}

void andor_delete(andor_str *andor)
{
    if(!andor)
        return;

    free(andor);
}

void andor_info(andor_str *andor)
{
    dprintf("Model: Fake ANDOR\n");
}

void andor_acquisition_start(andor_str *andor)
{
    is_acquiring = TRUE;
}

void andor_acquisition_stop(andor_str *andor)
{
    is_acquiring = FALSE;
}

int andor_is_acquiring(andor_str *andor)
{
    return is_acquiring;
}

void andor_cool(andor_str *andor)
{
}

image_str *andor_wait_image(andor_str *andor, double wait)
{
    image_str *image = NULL;

    static int id = 598000;

    if(is_acquiring){
        image = dataserver_get_frame(server, id);

        if(id >= server->length)
            id = 598000;
        else
            id ++;
    }

#if 0
    if(is_acquiring){
        int d;

        image = image_create(320, 270);
        image->time = time_current();

        for(d = 0; d < image->width*image->height; d++)
            image->data[d] = random_poisson(100);

        image_keyword_add_double(image, "EXPOSURE", 0.1, NULL);
    }
#endif

    usleep(7e4);

    return image;
}

image_str *andor_get_image(andor_str *andor)
{
    image_str *image = andor_wait_image(andor, 0);

    return image;
}

AT_64 get_int(AT_H handle, const wchar_t *name)
{
    AT_64 value = 0;

    return value;
}

AT_BOOL get_bool(AT_H handle, const wchar_t *name)
{
    AT_BOOL value = AT_FALSE;

    return value;
}

double get_float(AT_H handle, const wchar_t *name)
{
    double value = 0;

    return value;
}

static double get_float_min(AT_H handle, const wchar_t *name)
{
    double value = 0;

    return value;
}

static double get_float_max(AT_H handle, const wchar_t *name)
{
    double value = 0;

    return value;
}

AT_WC *get_string(AT_H handle, const wchar_t *name)
{
    static AT_WC buffer[128];

    buffer[0] = L'\0';

    return buffer;
}

int get_enum_index(AT_H handle, const wchar_t *name)
{
    int index = 0;

    return index;
}

AT_WC *get_enum_string(AT_H handle, const wchar_t *name)
{
    static AT_WC buffer[128];

    buffer[0] = L'\0';

    return buffer;
}
