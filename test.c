#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils.h"

#include "image.h"
#include "field.h"

int main(int argc, char **argv)
{
    field_str *field = field_create();

    char *string = "log id=test test2";
    char *id = NULL;
    char *text = NULL;
    int res = 0;

    res = sscanf(string, "log id=%ms %ms", &id, &text);

    dprintf("res=%d\n", res);


    return EXIT_SUCCESS;
}
