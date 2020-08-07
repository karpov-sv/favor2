#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "setup.h"

#include "utils.h"
#include "time_str.h"
#include "image.h"

#include "dataserver.h"

int main(int argc, char **argv)
{
    char *data = "IMG";
    dataserver_str *server = NULL;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    int first = 0;
    u_int64_t first_uuid = 0;
    int last = 0;
    u_int64_t last_uuid = 0;

    int info = FALSE;
    int dumpfits = FALSE;
    int dumpinfo = FALSE;
    int dumptiming = FALSE;

    parse_args(argc, argv,
               "data=%s", &data,
               "x1=%d", &x1,
               "y1=%d", &y1,
               "x2=%d", &x2,
               "y2=%d", &y2,
               "first=%d", &first,
               "first_uuid=%lld", &first_uuid,
               "last=%d", &last,
               "last_uuid=%lld", &last_uuid,
               "-dumpfits", &dumpfits,
               "-dumpinfo", &dumpinfo,
               "-dumptiming", &dumptiming,
                "-info", &info,
               "%s", &data,
               NULL);

    server = dataserver_create(data);

    if(server){
        if(first_uuid)
            first = dataserver_find_uuid(server, first_uuid);
        if(last_uuid)
            last = dataserver_find_uuid(server, last_uuid);

        first = MAX(first, dataserver_frame_first(server));
        last = MIN(last, dataserver_frame_last(server));
        if(last < first)
            last = first;

        if(info)
            dataserver_dump_info(server);

        if(dumpfits){
            int number;

            for(number = first; number <= last; number++){
                image_str *image = ((x2 > x1) && (y2 > y1))
                    ? dataserver_get_frame_cut(server, number, x1, y1, x2, y2)
                    : dataserver_get_frame(server, number);

                if(image){
                    char *filename = make_string("%08d.fits", number);

                    dprintf("%d\r", number);
                    image_dump_to_fits(image, filename);

                    free(filename);
                    image_delete(image);
                }
            }
        }

        if(dumpinfo){
            int number;

            for(number = first; number <= last; number++){
                char *timestamp = time_str_get_date_time(server->times[number]);

                printf("%d: %s %lld\n", number, timestamp, server->uuids[number]);

                free(timestamp);
            }
        }

       if(dumptiming){
            int number;

            for(number = first + 1; number <= last; number++){
                time_str time0 = server->times[number - 1];
                time_str time1 = server->times[number];

                printf("%d: %g\n", number, 1e-3*time_interval(time0, time1));
            }
        }

        dataserver_delete(server);
    }

    return EXIT_SUCCESS;
}
