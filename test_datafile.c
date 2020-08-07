#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

#include "datafile.h"

int main(int argc, char **argv)
{
    char *filename = NULL;
    datafile_str *file = NULL;
    
    parse_args(argc, argv,
               "%s", &filename,
               NULL);

    if(filename)
        file = datafile_create(filename);

    if(file){
        printf("%lld %lld\n", file->frame_first, file->frame_last);

        {
            image_str *image = datafile_get_frame(file, file->frame_last);

            image_dump_to_fits(image, "out.fits");

            image_delete(image);
        }        
        
        datafile_delete(file);
    }

    return EXIT_SUCCESS;
}
