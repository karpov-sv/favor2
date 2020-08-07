#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#include "utils.h"

#include "datafile.h"
#include "average.h"
#include "sextractor.h"

void print_focus_quality(image_str *image, const char *filename, int focus)
{
    struct list_head stars;
    FILE *file = fopen(filename, "w");
    record_str *record = NULL;
    double sum_xy = 0;
    double sum_x = 0;
    double sum_y = 0;
    double sum_x2 = 0;
    int N = 0;
    
    sextractor_get_stars_list(image, 100000, &stars);
    
    foreach(record, stars){
        double x = log10(record->flux);
        double y = log10(sqrt(record->a*record->a + record->b*record->b));
        
        fprintf(file, "%g %g %g %g %d\n", record->x, record->y, record->flux,
                sqrt(record->a*record->a + record->b*record->b), record->flags);

        if(!record->flags && record->flux > 0){
            sum_x += x;
            sum_x2 += x*x;
            sum_y += y;
            sum_xy += x*y;
            N ++;
        }
    }

    {
        double a = (N*sum_xy - sum_x*sum_y)/(N*sum_x2 - sum_x*sum_x);
        double b = (sum_x2*sum_y - sum_xy*sum_x)/(N*sum_x2 - sum_x*sum_x);

        //dprintf("%g %g %g %g %g %g %g\n", a, b, sum_x, sum_x2, sum_xy, sum_y, N);
        
        printf("%d %g %g %d %d\n", focus, a, b, list_length(&stars), N);
    }


    fclose(file);
    
    free_list(stars);
}

int main(int argc, char **argv)
{
    char *base = "out";
    int focus = 0;
    int first = 0;
    int length = 100;

    int is_median = FALSE;
    
    parse_args(argc, argv,
               "focus=%d", &focus,
               "first=%d", &first,
               "length=%d", &length,
               "base=%s", &base,
               "-median", &is_median,
               NULL);

    if(is_median){
        int d;

        for(d = first; d < first + length; d++){
            char *filename = make_string("%s_%04d_median.fits", base, d);
            char *filename_out = make_string("%s_%04d_median.stars", base, d);
            image_str *image = image_create_from_fits(filename);

            if(image){
                print_focus_quality(image, filename_out, d);
                
                image_delete(image);
            }

            free(filename_out);
            free(filename);
        }
    }    

    return EXIT_SUCCESS;
}
