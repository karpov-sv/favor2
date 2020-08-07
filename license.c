#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>

#include "utils.h"

char *get_mac_addr()
{
    struct ifreq s;
    char *result = NULL;

#if __linux__
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

    strcpy(s.ifr_name, "eth0");
    if (0 == ioctl(fd, SIOCGIFHWADDR, &s)) {
        int i;
        for (i = 0; i < 6; ++i){
            if(result)
                add_to_string(&result, ":");
            add_to_string(&result, "%02x", (unsigned char) s.ifr_addr.sa_data[i]);
        }
    }
#else
    result = make_string("00:01:02:30:40:50");
#endif

    return result;
}

char *get_encrypted_string(char *cmd)
{
    char *prog = cmd + strlen(cmd);
    char *mac = get_mac_addr();
    char *string = NULL;
    char *result = NULL;

    while(prog >= cmd && (prog > cmd && *(prog - 1) != '/'))
        prog --;

    string = make_string("%s %s", prog, mac);

    result = make_string("%s %s", prog, crypt(string, "$5$favor2")+10);

    free(mac);
    free(string);

    return result;
}

void check_license(char *cmd)
{
    char *filename = make_string("%s.license", cmd);
    FILE *file = fopen(filename, "r");
    int result = FALSE;

#ifdef USE_LICENSE
    if(file){
        char *string = get_encrypted_string(cmd);
        char *string2 = malloc(strlen(string) + 1);
        int res = fread(string2, strlen(string), 1, file);

        /* dprintf("1: %s\n2: %s\n", string, string2); */

        if(res && strncmp(string, string2, strlen(string)) == 0)
            result = TRUE;

        fclose(file);
    }
#else
    result = TRUE;
#endif

    if(!result){
        dprintf("License check failed\n");
        exit(1);
    } else
        dprintf("License OK\n");
}
