#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"

#include "license.h"

int main(int argc, char **argv)
{
    char *cmd = argv[0];
    int is_check = FALSE;

    parse_args(argc, argv,
               "-check", &is_check,
               "%s", &cmd,
               NULL);

    if(is_check)
        check_license(cmd);
    else {
        dprintf("Generating license for %s\n", cmd);

        printf("%s\n", get_encrypted_string(cmd));
    }

    return EXIT_SUCCESS;
}
