#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>

#include "setup.h"

#include "time_str.h"
#include "image.h"
#include "utils.h"
#include "dataserver.h"

static int dir_selector(const struct dirent *entry)
{
    if((entry->d_type == DT_DIR || entry->d_type == DT_LNK) &&
       fnmatch("????????_??????", entry->d_name, 0) == 0)
        return 1;
    else
        return 0;
}

static int file_selector(const struct dirent *entry)
{
    if((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) &&
       fnmatch("?????????????????.fits", entry->d_name, 0) == 0)
        return 1;
    else
        return 0;
}

dataserver_str *dataserver_create(char *path)
{
    dataserver_str *server = malloc(sizeof(dataserver_str));
    struct dirent **entry = NULL;
    int ndirs = scandir((const char *)path, &entry, dir_selector, alphasort);

    server->path = path;

    server->filenames = NULL;
    server->uuids = NULL;
    server->times = NULL;

    server->length = 0;

    void process_dir(char *dirpath)
    {
        struct dirent **file_entry = NULL;
        int nfiles = scandir((const char *)dirpath, &file_entry, file_selector, alphasort);
        int dd;

        server->filenames = realloc(server->filenames, sizeof(char *)*(server->length + nfiles));
        server->uuids = realloc(server->uuids, sizeof(u_int64_t)*(server->length + nfiles));
        server->times = realloc(server->times, sizeof(time_str)*(server->length + nfiles));

        for(dd = 0; dd < nfiles; dd++){
            time_str time = time_str_from_uuid_string(file_entry[dd]->d_name);

            server->filenames[server->length + dd] = make_string("%s/%s", dirpath, file_entry[dd]->d_name);
            server->times[server->length + dd] = time;
            server->uuids[server->length + dd] = time_str_get_uuid(time);

            free(file_entry[dd]);
        }

        server->length += nfiles;

        /* dprintf("%s: %d files\n", dirpath, nfiles); */

        free(file_entry);
    }

    if(ndirs >= 0){
        int d;

        for(d = 0; d < ndirs; d++){
            char *dirpath = make_string("%s/%s", path, entry[d]->d_name);

            process_dir(dirpath);

            free(dirpath);

            free(entry[d]);
        }

        /* Also look for FITS files in the dir itself, with no subdirs */
        process_dir(path);

        free(entry);
    }

    return server;
}

void dataserver_delete(dataserver_str *server)
{
    int d;

    for(d = 0; d < server->length; d++)
        if(server->filenames[d])
            free(server->filenames[d]);

    if(server->filenames)
        free(server->filenames);
    if(server->uuids)
        free(server->uuids);
    if(server->times)
        free(server->times);

    free(server);
}

void dataserver_dump_info(dataserver_str *server)
{
    char *time_first = time_str_get_date_time(server->times[0]);
    char *time_last = time_str_get_date_time(server->times[server->length - 1]);
    int d;

    printf("Dataserver at %s: %d files\n", server->path, server->length);
    printf("%d: %s\n", 0, time_first);
    printf("%d: %s\n", server->length - 1, time_last);

    printf("\n");

    for(d = 0; d < server->length; d++){
        printf("%d %lld %s\n", d, server->uuids[d], time_str_get_date_time_static(server->times[d]));
    }

    free(time_first);
    free(time_last);
}

int dataserver_frame_first(dataserver_str *server)
{
    return 0;
}

int dataserver_frame_last(dataserver_str *server)
{
    return server->length - 1;
}

image_str *dataserver_get_frame(dataserver_str *server, int number)
{
    if(number < server->length)
        return image_create_from_fits(server->filenames[number]);

    return NULL;
}

/* TODO: optimize for speed by decoding only necessary part of the frame */
image_str *dataserver_get_frame_cut(dataserver_str *server, int number, int x1, int y1, int x2, int y2)
{
    image_str *image = dataserver_get_frame(server, number);

    if(image){
        /* Got image - now let's cut it */
        image_str *cut = image_crop(image, x1, y1, x2, y2);

        image_delete(image);
        image = cut;
    }

    return image;
}

int dataserver_find_uuid(dataserver_str *server, u_int64_t uuid)
{
    u_int64_t *res = NULL;

    int cmpfn(const void *key, const void *value)
    {
        if(*(u_int64_t *)key < *(u_int64_t *)value)
            return -1;
        else if(*(u_int64_t *)key > *(u_int64_t *)value)
            return 1;
        else
            return 0;
    }

    res = bsearch(&uuid, server->uuids, server->length, sizeof(u_int64_t), cmpfn);

    printf("%lld: %p\n", uuid, res);

    if(res)
        return (res - server->uuids);
    else
        return -1;
}

int dataserver_find_time(dataserver_str *server, time_str time)
{
    return dataserver_find_uuid(server, time_str_get_uuid(time));
}
