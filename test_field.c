#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

#include "image.h"
#include "field.h"
#include "sextractor.h"
#include "match.h"

int main(int argc, char **argv)
{
    char *filename = "tests/4_226.fts";
    char *outname = NULL;
    char *dark_filename = NULL;
    char *filter = NULL;
    image_str *image = NULL;
    image_str *dark = NULL;
    double ra0 = 117.17;
    double dec0 = 17.83;
    double sr0 = 6;
    double aperture = 10.0;
    int is_wcs = FALSE;
    int is_blind = TRUE;
    int is_clean = FALSE;
    int is_tycho2 = FALSE;

    parse_args(argc, argv,
               "ra0=%lf", &ra0,
               "dec0=%lf", &dec0,
               "sr0=%lf", &sr0,
               "aperture=%lf", &aperture,
               "-wcs", &is_wcs,
               "-blind", &is_blind,
               "-clean", &is_clean,
               "-tycho2", &is_tycho2,
               "dark=%s", &dark_filename,
               "filter=%s", &filter,
               "out=%s", &outname,
               "%s", &filename,
               NULL);

    image = image_create_from_fits(filename);

    if(dark_filename){
        dark = image_create_from_fits(dark_filename);

        if(dark && dark->type != IMAGE_DOUBLE)
            image_delete_and_null(dark);
    }

    if(image){
        field_str *field = field_create();
        struct list_head objects;
        struct list_head stars;
        double ra0 = 0;
        double dec0 = 0;

        field->verbose = TRUE;

        if(is_blind)
            image->coords = coords_empty();

        if(dark || is_clean){
            image_str *new = image_create_with_type(image->width, image->height, IMAGE_DOUBLE);
            int d;

            if(dark){
                dprintf("Subtracting the dark/bias frame\n");
                if(image->type == IMAGE_DOUBLE)
                    for(d = 0; d < image->width*image->height; d++)
                        new->double_data[d] = image->double_data[d] - dark->double_data[d];
                else
                    for(d = 0; d < image->width*image->height; d++)
                        new->double_data[d] = image->data[d] - dark->double_data[d];
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
                dprintf("Cleaning the data for ANDOR-specific artifacts\n");
                image_clean_stripes(new);
            }

            image_copy_properties(image, new);

            image_delete(image);
            image = new;
        }

        sextractor_get_stars_list_aper(image, 100000, &objects, aperture);

        if(coords_is_empty(&image->coords))
            field->coords = blind_match_coords_from_list(&objects, image->width, image->height);
        else
            field->coords = image->coords;

        /* 3 pixels as a guess of coordinate uncertainty */
        field->max_dist = 5.0*coords_get_pixscale(&field->coords);

        coords_get_ra_dec(&field->coords, 0.5*image->width, 0.5*image->height, &ra0, &dec0);

        if(is_tycho2){
            tycho2_get_stars_list(ra0, dec0,
                                  0.5*coords_distance(&field->coords, 0, 0, image->width, image->height), 100000, &stars);
            dprintf("Calibrating to Tycho2 catalogue, no R band data will be available\n");
        } else {
            /* By default we will use WBVR catalogue */
            wbvr_get_stars_list(ra0, dec0,
                                0.5*coords_distance(&field->coords, 0, 0, image->width, image->height), 100000, &stars);
            dprintf("Calibrating to WBVR catalogue, only bright stars will be matched\n");
        }

        /* Guess the filter from FITS keywords */
        if(filter)
            field_set_filter(field, filter);
        else
            field_set_filter(field, image_keyword_get_string(image, "FILTER"));

        field_set_stars(field, &stars, is_tycho2 ? "Tycho2" : "WBVR");

        field_calibrate(field, &objects);

        field_annotate_image(field, image);

        image->coords = field->coords;

        if(outname)
            image_dump_to_fits(image, outname);

        free_list(objects);
        free_list(stars);

        image_delete(image);
    }

    return EXIT_SUCCESS;
}
