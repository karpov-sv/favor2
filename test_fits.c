#include <stdlib.h>
#include <unistd.h>
#include <fitsio.h>

#include "image.h"
#include "utils.h"

int main(int argc, char **argv)
{
    char *filename = "tests/4_226.fts";
    char *outname = "out.1.fits";
    
    parse_args(argc, argv,
               "%s", &filename,
               NULL);

    if(filename){
        image_str *image = image_create_from_fits(filename);

        if(image){
            image_keyword_add_int64(image, "FRAMENUMBER", 1000, NULL);

            image->time = time_current();
            
            dprintf("%lld\n", image_keyword_get_int64(image, "FRAMENUMBER"));

            image_dump_to_fits(image, outname);
            
            image_delete(image);
        }
    }

    return EXIT_SUCCESS;
}
