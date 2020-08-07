#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include "utils.h"

#include "image.h"
#include "average.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

//#include <smmintrin.h>

#include "utils.h"

static void ds9_set_filename(char *filename)
{
    char *command = make_string("xpaset -p ds9 file fits %s", filename);
    //char *command = make_string("xpaset -p ds9 file fits %s && xpaset -p ds9 scale limits 100 1024", filename);
    // char *command = make_string("xpaset -p ds9 file fits %s && xpaset -p ds9 scale limits 0 400", filename);
    
    system(command);

    free(command);
}

inline double stoptime(void) {
   struct timeval t;
   gettimeofday(&t,NULL);
   return (double) t.tv_sec + t.tv_usec/1000000.0;
}

int main(int argc, char **argv)
{
    average_image_str *avg = average_image_create(100);
    image_str *image = NULL;
    int width = 2560;
    int height = 2160;
    int R = 200;
    int d;
    u_int16_t value = 0;
        
    parse_args(argc, argv,
               "width=%d", &width,
               "height=%d", &height,
               "R=%d", &R,
               NULL);
    
    image = image_create(width, height);

    for(d = 0; d < R; d++){
        int dd;
        
        for(dd = 0; dd < image->width*image->height; dd++){
            image->data[dd] = value++ % 201;

            if(value > 3333)
                value = 0;
        }

        /* image_dump_to_fits(image, "/tmp/out.fits"); */

        /* ds9_set_filename("/tmp/out.fits"); */
        
        average_image_add(avg, image);
    }

    average_image_delete(avg);
    image_delete(image);
    
    return EXIT_SUCCESS;
}
