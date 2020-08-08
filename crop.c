#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#include "utils.h"

#include "image.h"
#include "match.h"

int main(int argc, char **argv)
{
    char *filename = NULL;
    char *outname = NULL;
    char *dark_filename = NULL;
    char *flat_filename = NULL;
    char *wcs_filename = NULL;
    image_str *dark = NULL;
    image_str *flat = NULL;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int width = 0;
    int height = 0;
    int downscale = 1;

    double refine_radius = 1.0;

    int is_clean = FALSE;
    int is_linearize = FALSE;
    int is_match = FALSE;
    int is_refine = FALSE;
    int is_compress = FALSE;
    int is_double = FALSE;
    int is_fix_avg = FALSE;

    parse_args(argc, argv,
               "x0=%d", &x0,
               "y0=%d", &y0,
               "x1=%d", &x1,
               "y1=%d", &y1,
               "width=%d", &width,
               "height=%d", &height,
               "downscale=%d", &downscale,
               "dark=%s", &dark_filename,
               "flat=%s", &flat_filename,
               "-clean", &is_clean,
               "-linearize", &is_linearize,
               "-match", &is_match,
               "-refine", &is_refine,
               "refine_radius=%lf", &refine_radius,
               "match_degree=%d", &wcs_match_degree,
               "refine_degree=%d", &wcs_refine_degree,
               "wcs=%s", &wcs_filename,
               "-compress", &is_compress,
               "-double", &is_double,
               "-fix_avg", &is_fix_avg,
               "%s", &filename,
               "%s", &outname,
               NULL);

    if(dark_filename){
        dark = image_create_from_fits(dark_filename);

        if(dark && dark->type != IMAGE_DOUBLE)
            image_delete_and_null(dark);
    }

    if(dark && flat_filename){
        flat = image_create_from_fits(flat_filename);

        if(flat && flat->type != IMAGE_DOUBLE)
            image_delete_and_null(flat);
    }

    if(width && height){
        x1 = x0 + 0.5*width;
        y1 = y0 + 0.5*height;
        x0 = x0 - 0.5*width;
        y0 = y0 - 0.5*height;
    }

    if(is_compress && outname)
        outname = make_string("%s[compress]", outname);

    if(filename){
        image_str *image = image_create_from_fits(filename);

        if(image){
            if(dark || is_clean || is_linearize){
                image_str *new = image_create_with_type(image->width, image->height, IMAGE_DOUBLE);
                int d;

                image_copy_properties(image, new);

                if(dark){
                    /* dprintf("Subtracting the dark/bias frame\n"); */
                    if(image->type == IMAGE_DOUBLE)
                        for(d = 0; d < image->width*image->height; d++)
                            new->double_data[d] = image->double_data[d] - dark->double_data[d];
                    else
                        for(d = 0; d < image->width*image->height; d++)
                            new->double_data[d] = image->data[d] - dark->double_data[d];

                    if(flat)
                        for(d = 0; d < image->width*image->height; d++)
                            new->double_data[d] = new->double_data[d]/flat->double_data[d];
                } else {
                    /* Just copy the pixel values */
                    if(image->type == IMAGE_DOUBLE)
                        for(d = 0; d < image->width*image->height; d++)
                            new->double_data[d] = image->double_data[d];
                    else
                        for(d = 0; d < image->width*image->height; d++)
                            new->double_data[d] = image->data[d];
                }

                if(is_clean){
                    /* dprintf("Cleaning the data for ANDOR-specific artifacts\n"); */
                    image_clean_stripes(new);
                }
                if(is_linearize){
                    /* Linearization */
                    image_linearize(new, NULL);
                }

                image_delete(image);
                image = new;
            }

            if(is_double && image->type != IMAGE_DOUBLE){
                image_str *new = image_convert_to_double(image);

                image_delete(image);
                image = new;
            }

            if(downscale > 1){
                image_str *new = image_downscale(image, downscale);

                image_delete(image);
                image = new;
            }

            if(is_match)
                image->coords = blind_match_coords(image);

            if(is_refine)
                image_coords_refine(image, refine_radius);

            if(wcs_filename){
                image_str *ext = image_create_from_fits(wcs_filename);

                if(ext){
                    image->coords = ext->coords;
                    image_delete(ext);
                }
            }

            if(is_fix_avg){
                int d;
                /* Fix off-by-one error in some pre-20160306 AVG images being
                   summed over 99 frames but divided by 100 */

                if(image->type != IMAGE_DOUBLE){
                    dprintf("AVG image should be of DOUBLE type!\n");
                    exit(EXIT_FAILURE);
                }

                if(image_keyword_get_int(image, "AVGFIX") == 1){
                    dprintf("AVG image is already fixed!\n");
                    exit(EXIT_FAILURE);
                }

                for(d = 0; d < image->width*image->height; d++)
                    image->double_data[d] *= 100.0/99.0;

                image_keyword_add_int(image, "AVGFIX", 1, "Image fixed for AVG off-by-one bug");
            }

            if(x1 > x0 && y1 > y0){
                image_str *crop = image_crop(image, x0, y0, x1, y1);

                image_keyword_add_int(crop, "CROP_X0", x0, "Cropping region");
                image_keyword_add_int(crop, "CROP_Y0", y0, "Cropping region");
                image_keyword_add_int(crop, "CROP_X1", x1, "Cropping region");
                image_keyword_add_int(crop, "CROP_Y1", y1, "Cropping region");

                if(outname)
                    image_dump_to_fits(crop, outname);

                image_delete(crop);
            } else if(outname)
                    image_dump_to_fits(image, outname);

            image_delete(image);
        }
    }

    return EXIT_SUCCESS;
}
