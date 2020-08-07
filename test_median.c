#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "utils.h"

#include "datafile.h"
//#include "median_image.h"
#include "median_image.h"

int main(int argc, char **argv)
{
    char *filename = "IMG/100319_215122.GRB2";
    datafile_str *file = NULL;
    median_image_str *median = median_image_create(100);
    int d;
    
    parse_args(argc, argv,
               "%s", &filename,
               NULL);

    file = datafile_create(filename);
    
    for(d = 0; d < 101; d++){
        image_str *image = datafile_get_frame(file, file->frame_first + d);

        printf("%d\n", d);

        if(median_image_is_full(median)){
            int n0 = 0;
            int n3 = 0;
            int n4 = 0;
            int dd;

            for(dd = 0; dd < median->width*median->height; dd++){
                double excess = median_image_excess(median, image, dd, 0);

                /* printf("%g\n", excess); */
                
                if(excess > 4)
                    n4 ++;
                else if(excess > 3)
                    n3 ++;

                n0 ++;
            }

            printf("%d %d %d\n", n0, n3, n4);
        }

        {
            struct timeval t1, t2;

            gettimeofday(&t1, NULL);
            median_image_add(median, image);
            gettimeofday(&t2, NULL);

            printf("delta = %g\n", (t2.tv_sec + 1e-6*t2.tv_usec - t1.tv_sec - 1e-6*t1.tv_usec));
        }

    }

    median_image_delete(median);
    
    return EXIT_SUCCESS;
}
