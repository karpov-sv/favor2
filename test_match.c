#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "lists.h"
#include "image.h"
#include "sextractor.h"
#include "stars.h"
#include "match.h"

int main(int argc, char **argv){
    char *filename = "tests/4_226.fts";
    char *filename_out = "out.fits";
    image_str *image = NULL;

    double ra0 = 117.17;
    double dec0 = 17.83;
    double sr0 = 6;

    double ra1 = 120;
    double ra2 = 180;
    double dra = 2;
    double dec1 = -20;
    double dec2 = 20;
    double ddec = 2;
    
    double is_find = FALSE;
    
    parse_args(argc, argv,
               "ra0=%lf", &ra0,
               "dec0=%lf", &dec0,               
               "sr0=%lf", &sr0,
               "ra1=%lf", &ra1,
               "dec1=%lf", &dec1,
               "ra2=%lf", &ra2,
               "dec2=%lf", &dec2,
               "dra=%lf", &dra,
               "ddec=%lf", &ddec,
               "-find", &is_find,
               "%s", &filename,
               "%s", &filename_out,
               NULL);
    
    image = image_create_from_fits(filename);

    if(image){
        struct list_head objects;
        struct list_head stars;
        double sigma = 0;
        
        sextractor_get_stars_list(image, 100, &objects);

        if(is_find){
            double ra;
            double dec;

            for(ra = ra1; ra <= ra2; ra += dra)
                for(dec = dec1; dec <= dec2; dec += ddec){
                    coords_str coords;
                    double sigma = 0;
                
                    dprintf("%g %g\n", ra, dec);
                
                    tycho2_get_stars_list(ra, dec, sr0, 100, &stars);

                    coords = match_coords_get_sigma(&objects, &stars, ra, dec, &sigma);

                    if(!coords_is_empty(&coords)){
                        dprintf("Matched with pixel scale %g\"/pix and sigma %g\"\n",
                                coords_get_pixscale(&coords)*3600, sigma*3600);
                        dprintf("ra0 = %g, dec0 = %g\n", coords.ra0, coords.dec0);

                        image->coords = coords;
                    
                        image_dump_to_fits(image, filename_out);

                        exit(0);
                    }

                    free_list(stars);
                }
        } else {
            tycho2_get_stars_list(ra0, dec0, sr0, 100, &stars);
            
            image->coords = match_coords_get_sigma(&objects, &stars, ra0, dec0, &sigma);
            
            if(!coords_is_empty(&image->coords)){
                dprintf("Matched with pixel scale %g\"/pix and sigma %g\"\n",
                        coords_get_pixscale(&image->coords)*3600, sigma*3600);
                dprintf("ra0 = %g, dec0 = %g\n", image->coords.ra0, image->coords.dec0);
                
                image_dump_to_fits(image, filename_out);
                
                exit(0);
            }
        }
        
        free_list(objects);
        
        image_delete(image);
    }
    
    return EXIT_SUCCESS;
}
