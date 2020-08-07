#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "setup.h"

#include "utils.h"
#include "time_str.h"
#include "image.h"
#include "match.h"

#include "dataserver.h"

int main(int argc, char **argv)
{
    char *data = "IMG";
    dataserver_str *server = NULL;
    int first = 0;
    u_int64_t first_uuid = 0;
    int last = 0;
    u_int64_t last_uuid = 0;

    int is_match = FALSE;
    int is_clean = FALSE;

    int nframes = 100;
    char *outdir = NULL;
    char *type = "avg";

    char *darkname = NULL;
    char *flatname = NULL;
    image_str *dark = NULL;
    image_str *flat = NULL;
    double dark_mean = 0;
    double flat_mean = 0;

    parse_args(argc, argv,
               "data=%s", &data,
               "first=%d", &first,
               "first_uuid=%lld", &first_uuid,
               "last=%lld", &last,
               "last_uuid=%lld", &last_uuid,
               "nframes=%d", &nframes,
               "outdir=%s", &outdir,
               "type=%s", &type,
               "dark=%s", &darkname,
               "flat=%s", &flatname,
               "-match", &is_match,
               "-clean", &is_clean,
               "%s", &data,
               NULL);

    if(!outdir)
        outdir = make_string("%s/coadd", data);

    if(darkname)
        dark = image_create_from_fits(darkname);
    if(flatname)
        flat = image_create_from_fits(flatname);

    if(dark)
        dark_mean = image_mean(dark);
    if(flat)
        flat_mean = image_mean(flat);

    server = dataserver_create(data);

    if(!server)
        return EXIT_FAILURE;

    if(first_uuid)
        first = dataserver_find_uuid(server, first_uuid);
    if(last_uuid)
        last = dataserver_find_uuid(server, last_uuid);

    first = MAX(first, 0);
    last = MIN(last, server->length - 1);

    if(last < first)
        last = first;

    if(!last || last == first)
        last = server->length - 1;

    if(!nframes || nframes > last - first + 1)
        nframes = last - first + 1;

    {
        image_str *sum = NULL;
        int length = 0;
        double exposure = 0;
        int d;

        dprintf("Coadding from %s (%d - %d, every %d frames) to %s, type %s\n", server->path, first, last, nframes, outdir, type);

        for(d = first; d <= last; d++){
            image_str *image = dataserver_get_frame(server, d);

            if(!sum){
                sum = image_create_double(image->width, image->height);
                image_copy_properties(image, sum);
            }

            image_add(sum, image);
            length ++;
            exposure += image_keyword_get_double(image, "EXPOSURE");

            dprintf("\t%d / %d\r", d - first, last - first);

            if(length == nframes){
                char *filename = make_string("%s/%04d%02d%02d_%02d%02d%02d_%d.%s.fits", outdir,
                                             sum->time.year, sum->time.month, sum->time.day,
                                             sum->time.hour, sum->time.minute, sum->time.second,
                                             image_keyword_get_int(sum, "CHANNEL ID"), type);
                int dd;

                image_keyword_add_int(sum, "AVERAGED", length, "Total number of frames coadded");
                image_keyword_add_double(sum, "TOTAL EXPOSURE", exposure, "Total exposure of coadded frames");
                image_keyword_add_double(sum, "EXPOSURE", exposure/length, "Normalized (average) exposure");

                for(dd = 0; dd < sum->width*sum->height; dd++)
                    sum->double_data[dd] *= 1./length;

                /* Image processing */
                if(dark && dark->width == sum->width && dark->height == sum->height){
                    dprintf("Subtracting DARK frame: %s\n", darkname);

                    for(dd = 0; dd < sum->width*sum->height; dd++)
                        sum->double_data[dd] -= dark->double_data[dd];

                    if(flat && flat->width == sum->width && flat->height == sum->height){
                        dprintf("Correcting for FLAT frame: %s\n", flatname);

                        for(dd = 0; dd < sum->width*sum->height; dd++)
                            sum->double_data[dd] *= (flat_mean - dark_mean)/(flat->double_data[dd] - dark->double_data[dd]);
                    }
                }

                if(is_clean)
                    image_clean_stripes(sum);

                mkdir(outdir, 0755);

                if(is_match)
                    sum->coords = blind_match_coords(sum);

                dprintf("%s\n", filename);

                image_dump_to_fits(sum, filename);

                image_delete_and_null(sum);
                length = 0;

                free(filename);
            }

            image_delete(image);
        }
    }

    dataserver_delete(server);

    return EXIT_SUCCESS;
}
