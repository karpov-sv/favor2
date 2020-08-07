#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <xmmintrin.h>
#include <sys/mman.h>

#include "utils.h"

#include "lists.h"
#include "realtime.h"
#include "realtime_types.h"

/* Delta arrays - possible steps from given pixel */
static int dx[] = {0, 0,  0, 1, 1,  1, -1, -1, -1};
static int dy[] = {0, 1, -1, 1, 0, -1,  1,  0, -1};
static int dN = 9;

#define IS_PIXEL_VALID(image, x, y) ((x) >= 0 && (x) < (image)->width && (y) >= 0 && (y) < (image)->height)

/* Get the direction to the nearest local maximum as an index in delta
   arrays */
static inline int get_max_n_idx(average_image_str *avg, image_str *image, int x, int y, int type)
{
    int d;
    int max_value = 0;
    int max_id = 0;

    for(d = 0; d < dN; d++){
        if(IS_PIXEL_VALID(image, x + dx[d], y + dy[d])){
            int value = 0;

            if(type == 0)
                value = EXCESS(avg, image, x + dx[d], y + dy[d]);
            else if(type == 1)
                value = MEDIAN(avg, x + dx[d], y + dy[d]);
            else
                value = MAD(avg, x + dx[d], y + dy[d]);

            if(d == 0 || value > max_value){
                max_value = value;
                max_id = d;
            }
        }
    }

    return max_id;
}

static inline int get_min_n_idx(average_image_str *avg, image_str *image, int x, int y, int type)
{
    int d;
    int min_value = 0;
    int min_id = 0;

    for(d = 0; d < dN; d++){
        if(IS_PIXEL_VALID(image, x + dx[d], y + dy[d])){
            int value = 0;

            if(type == 0)
                value = EXCESS(avg, image, x + dx[d], y + dy[d]);
            else if(type == 1)
                value = MEDIAN(avg, x + dx[d], y + dy[d]);
            else
                value = MAD(avg, x + dx[d], y + dy[d]);

            if(d == 0 || value < min_value){
                min_value = value;
                min_id = d;
            }
        }
    }

    return min_id;
}

/* Finds the locally reachable maximum position */
static inline void get_reachable_maximum_xy(average_image_str *avg, image_str *image, int x0, int y0, int *x_ptr, int *y_ptr, int type, int maxsteps)
{
    int x = x0;
    int y = y0;
    int id = 0;
    int Nsteps = 0;

    while((id = get_max_n_idx(avg, image, x, y, type)) && (!maxsteps || Nsteps < maxsteps)){
        x += dx[id];
        y += dy[id];
        Nsteps ++;
    }

    *x_ptr = x;
    *y_ptr = y;
}

static inline void get_reachable_minimum_xy(average_image_str *avg, image_str *image, int x0, int y0, int *x_ptr, int *y_ptr, int type, int maxsteps)
{
    int x = x0;
    int y = y0;
    int id = 0;
    int Nsteps = 0;

    while((id = get_min_n_idx(avg, image, x, y, type)) && (!maxsteps || Nsteps < maxsteps)){
        x += dx[id];
        y += dy[id];
        Nsteps ++;
    }

    *x_ptr = x;
    *y_ptr = y;
}

/* Warning: time-critical function! */
static region_str *extract_region(realtime_str *realtime, image_str *image, region_str **mask, int x0, int y0)
{
    region_str *region = (region_str *)calloc(1, sizeof(region_str));
    int stack_length = 100;
    int stack_pos = 0;
    int *stack_x = (int *)calloc(stack_length, sizeof(int));
    int *stack_y = (int *)calloc(stack_length, sizeof(int));
    int x = x0;
    int y = y0;

    average_image_str *avg = realtime->avg;
    image_str *badpix = realtime->badpix;
    image_str *flat = realtime->flat;

    init_list(region->pixels);

    region->replaced = NULL;
    region->npixels = 0;
    region->npixels_significant = 0;
    region->is_edge = FALSE;
    region->is_noise = FALSE;
    region->record = NULL;

    inline void claim_pixel(x, y)
    {
        pixel_str *pixel = (pixel_str *)calloc(1, sizeof(pixel_str));

        pixel->x = x;
        pixel->y = y;

        pixel->excess = EXCESS(avg, image, x, y);
        pixel->flux = PIXEL(image, x, y) - MEDIAN(avg, x, y);
        pixel->flux_err = sqrt(MAD(avg, x, y)*MAD(avg, x, y) + pixel->flux/realtime->gain);

        if(flat && isfinite(PIXEL_DOUBLE(flat, x, y)) && PIXEL_DOUBLE(flat, x, y) > 0){
            pixel->flux /= PIXEL_DOUBLE(flat, x, y);
            pixel->flux_err /= PIXEL_DOUBLE(flat, x, y);
        }

        add_to_list(region->pixels, pixel);

        mask[y*image->width + x] = region;

        region->npixels ++;

        if(pixel->excess > 4 && !(badpix && PIXEL(badpix, x, y)))
            region->npixels_significant ++;
    }

    claim_pixel(x, y);

    /* Recursive descent */
    while(TRUE){
        int i = 0;
        double excess0 = EXCESS(avg, image, x, y);

        /* Find and claim all destinations leading down from this point */
        for(i = 1; i < dN; i++){
            int x1 = x + dx[i];
            int y1 = y + dy[i];

            if(IS_PIXEL_VALID(image, x1, y1)){
                if(EXCESS(avg, image, x1, y1) > 3e10 || /* We may also go up, if it is higher than 3 */
                   (EXCESS(avg, image, x1, y1) <= excess0 + 1e-10 &&
                    EXCESS(avg, image, x1, y1) > 3 /* Lower level we will descend */)){
                    if(!mask[y1*image->width + x1]){
                        if(stack_pos == stack_length){
                            stack_length *= 2;
                            stack_x = realloc(stack_x, sizeof(int)*stack_length);
                            stack_y = realloc(stack_y, sizeof(int)*stack_length);
                        }

                        claim_pixel(x1, y1);

                        stack_x[stack_pos] = x1;
                        stack_y[stack_pos] = y1;

                        stack_pos ++;
                    } else if(mask[y1*image->width + x1] != region){
                        /* Encounter already claimed region. Let's eat it! */
                        /* Such an encounter is due to the fact that each point
                           has only one reachable maximum, but may be
                           recursively reached from several ones */
                        region_str *existing = mask[y1*image->width + x1];

                         while(existing->replaced)
                            existing = existing->replaced;

                        if(existing != region){
                            add_list_to_list(region->pixels, existing->pixels);

                            region->npixels += existing->npixels;
                            region->npixels_significant += existing->npixels_significant;

                            existing->replaced = region;
                        }

                        if(existing->is_edge)
                            region->is_edge = TRUE;
                    }
                }
            } else
                region->is_edge = TRUE;
        }

        /* Go to the stack upper point, if any */
        if(stack_pos > 0){
            stack_pos --;
            x = stack_x[stack_pos];
            y = stack_y[stack_pos];
        } else
            /* Processed all points */
            break;
    }

    free(stack_x);
    free(stack_y);

    return region;
}

inline record_str *record_from_region(realtime_str *realtime, region_str *region, image_str *image)
{
    record_str *record = (record_str *)malloc(sizeof(record_str));
    pixel_str *pixel = NULL;
    double sum_x = 0;
    double sum_x2 = 0;
    double sum_y = 0;
    double sum_y2 = 0;
    double sum_xy = 0;
    double sum_wx = 0;
    double sum_wy = 0;
    double sum_flux = 0;
    double sum_xerr = 0;
    double sum_x2err = 0;
    double sum_yerr = 0;
    double sum_y2err = 0;
    double sum_flux_err2 = 0;
    double sum_weight = 0;
    double sum_excess = 0;
    double sum_excess2 = 0;
    double X2 = 0;
    double Y2 = 0;
    double XY = 0;
    double peakflux = 0;
    int N = 0;
    int N0 = 0;
    int d;

    record->flags = 0;

    if(region->is_edge)
        record->flags |= RECORD_TRUNCATED;

    if(region->is_noise)
        record->flags |= RECORD_NOISE;

    inline void add_values(int x, int y, double excess, double flux, double flux_err, double weight)
    {
        sum_wx += weight*flux*x;
        sum_wy += weight*flux*y;

        sum_x += flux*x;
        sum_x2 += flux*x*x;
        sum_y += flux*y;
        sum_y2 += flux*y*y;
        sum_xy += flux*x*y;
        sum_weight += weight*flux;
        sum_flux += flux;
        sum_xerr += x*flux_err*flux_err;
        sum_x2err += x*x*flux_err*flux_err;
        sum_yerr += y*flux_err*flux_err;
        sum_y2err += y*y*flux_err*flux_err;
        sum_flux_err2 += flux_err*flux_err;
        sum_excess += excess;
        sum_excess2 += excess*excess;

        N++;
    }

    /* Routine to compute record parameters */
    inline void compute_record_params()
    {
        record->x = sum_wx/sum_weight;
        record->y = sum_wy/sum_weight;
        record->flux = sum_flux;
        record->flux_err = sqrt(sum_flux_err2);
        record->excess = sqrt(sum_excess2);

        /* Second-order moments */
        X2 = sum_x2/sum_flux - sum_x*sum_x/sum_flux/sum_flux;
        Y2 = sum_y2/sum_flux - sum_y*sum_y/sum_flux/sum_flux;
        XY = sum_xy/sum_flux - sum_x*sum_y/sum_flux/sum_flux;

        /* Simple error-propagation estimate for ucorrelated noise */
        record->sigma_x = sqrt(MAX(0, sum_x2err - 2*sum_xerr*sum_x/sum_flux + sum_flux_err2*sum_x*sum_x/sum_flux/sum_flux))/sum_flux;
        record->sigma_y = sqrt(MAX(0, sum_y2err - 2*sum_yerr*sum_y/sum_flux + sum_flux_err2*sum_y*sum_y/sum_flux/sum_flux))/sum_flux;

        /* Pixelization error */
        record->sigma_x = hypot(record->sigma_x, 0.289/sqrt(N));
        record->sigma_y = hypot(record->sigma_y, 0.289/sqrt(N));

        /* Safety check */
        if(!isfinite(record->sigma_x))
            record->sigma_x = 0.289;
        if(!isfinite(record->sigma_y))
            record->sigma_y = 0.289;

        record->sigma_x = MAX(record->sigma_x, 0.5);
        record->sigma_y = MAX(record->sigma_y, 0.5);

        /* Handling singular cases */
        if(X2*Y2 - XY*XY < 1./12/12){
            X2 += 1./12;
            Y2 += 1./12;
        }

        /* Formulae from the SExtractor manual, p.29 */
        record->theta = 0.5*atan2(2*XY, X2 - Y2);
        record->a = sqrt(fabs(0.5*(X2 + Y2) + sqrt(0.25*(X2 - Y2)*(X2 - Y2) + XY*XY)));
        record->b = sqrt(fabs(0.5*(X2 + Y2) - sqrt(0.25*(X2 - Y2)*(X2 - Y2) + XY*XY)));

        /* Is the record elongated enough to mention it? */
        if((record->b > 1 && record->a > realtime->meteor_elongation_threshold*record->b) ||
           record->a > realtime->meteor_length_threshold)
            record->flags |= RECORD_ELONGATED;

        /* TODO: Check if record is large enough to be BAD */
    }

    /* First iteration - process all pixels extracted and estimate initial parameters' guess */
    foreach(pixel, region->pixels){
        add_values(pixel->x, pixel->y, pixel->excess, pixel->flux, pixel->flux_err, 1.0);

        peakflux = MAX(peakflux, pixel->flux);

        /* FIXME: specify saturation level externally */
        if(PIXEL(image, pixel->x, pixel->y) >= realtime->saturation_level)
            record->flags |= RECORD_SATURATED;

        /* if(!PIXEL(image, pixel->x, pixel->y)) */
        /*     exit(1); */

        /* FIXME: this is for debug only, don't forget to remove! */
        //image->data[pixel->y*image->width + pixel->x] = 0;//(short)region;
    }

    compute_record_params();

    /* Second iteration - collect all the pixels in sufficiently large
       elliptical window around the record. Not for ELONGATED or BAD ones. */
    for(d = 0; d < 3; d++){
        N0 = N;

        /* Cleanup */
        sum_x = 0;
        sum_x2 = 0;
        sum_y = 0;
        sum_y2 = 0;
        sum_xy = 0;
        sum_wx = 0;
        sum_wy = 0;
        sum_flux = 0;
        sum_xerr = 0;
        sum_x2err = 0;
        sum_yerr = 0;
        sum_y2err = 0;
        sum_flux_err2 = 0;
        sum_weight = 0;
        sum_excess = 0;
        sum_excess2 = 0;
        X2 = 0;
        Y2 = 0;
        XY = 0;
        N = 0;

        if(!(record->flags & RECORD_ELONGATED) && !(record->flags & RECORD_BAD)){
            double extension = 3;
            double a = MAX(3, record->a*extension);
            double b = MAX(3, record->b*extension);
            double theta = record->theta;
            double CXX = cos(theta)*cos(theta)/a/a + sin(theta)*sin(theta)/b/b;
            double CYY = sin(theta)*sin(theta)/a/a + cos(theta)*cos(theta)/b/b;
            double CXY = 2*cos(theta)*sin(theta)*(1.0/a/a - 1.0/b/b);
            double dy = 2*sqrt(CXX)/sqrt(4*CXX*CYY - CXY*CXY);
            int ymin = ceil(record->y - dy);
            int ymax = floor(record->y + dy);
            int y;
            int added = 0;

            double cxx = cos(theta)*cos(theta)/record->a/record->a + sin(theta)*sin(theta)/record->b/record->b;
            double cyy = sin(theta)*sin(theta)/record->a/record->a + cos(theta)*cos(theta)/record->b/record->b;
            double cxy = 2*cos(theta)*sin(theta)*(1.0/record->a/record->a - 1.0/record->b/record->b);

            /* Crop frame edges */
            ymin = MAX(0, ymin);
            ymax = MIN(image->height - 1, ymax);

            if(ymin == 0 || ymax == image->height - 1)
                record->flags |= RECORD_TRUNCATED;

            for(y = ymin; y <= ymax; y++){
                double yc = y - record->y;
                double xc = +CXY*yc/2/CXX;
                double dx = sqrt((CXY*CXY - 4*CXX*CYY)*yc*yc + 4*CXX)/2/CXX;
                int xmin = ceil(record->x + xc - dx);
                int xmax = floor(record->x + xc + dx);
                int x;

                /* Do we really need it? */
                if(xmin > xmax){
                    int tmp = xmax;

                    xmax = xmin;
                    xmin = tmp;
                }

                /* Crop frame edges */
                xmin = MAX(0, xmin);
                xmax = MIN(image->width - 1, xmax);

                if(xmin == 0 || xmax == image->width - 1)
                    record->flags |= RECORD_TRUNCATED;

                for(x = xmin; x <= xmax; x++){
                    double excess = EXCESS(realtime->avg, image, x, y);
                    /* FIXME: we should linearize the flux! */
                    double flux = PIXEL(image, x, y) - MEDIAN(realtime->avg, x, y);
                    double flux_err = sqrt(MAD(realtime->avg, x, y)*MAD(realtime->avg, x, y) + MAX(0, flux)/realtime->gain);

                    double weight = exp(-0.5*(cxx*(x - record->x)*(x - record->x) + cxy*(x - record->x)*(y - record->y) + cyy*(y - record->y)*(y - record->y)));

                    if(realtime->flat && isfinite(PIXEL_DOUBLE(realtime->flat, x, y)) && PIXEL_DOUBLE(realtime->flat, x, y) > 0){
                        flux /= PIXEL_DOUBLE(realtime->flat, x, y);
                        flux_err /= PIXEL_DOUBLE(realtime->flat, x, y);
                    }

                    if(excess > 0){
                        add_values(x, y, excess, flux, flux_err, weight);
                        added ++;
                    }
                }
            }

            /* Re-compute only if more pixels are collected */
            //if(N > N0)
                    compute_record_params();
        }
    }

    /* Temporary debug code */
    if(FALSE && record->flux > 2200 && record->x > 970 && record->x < 1010){
        int x0 = record->x;
        int y0 = record->y;
        image_str *crop = image_crop(image, x0 - 10, y0 - 10, x0 + 11, y0 + 11);
        int x;
        int y;

        image_dump_to_fits(crop, "out_crop.fits");

        crop = image_create_double(crop->width, crop->height);

        for(x = x0 - 10; x <= x0 + 10; x++)
            for(y = y0 - 10; y <= y0 + 10; y++)
                PIXEL_DOUBLE(crop, x - x0 + 10, y - y0 + 10) = EXCESS(realtime->avg, image, x, y);

        image_dump_to_fits(crop, "out_crop.e.fits");

        for(x = x0 - 10; x <= x0 + 10; x++)
            for(y = y0 - 10; y <= y0 + 10; y++)
                PIXEL_DOUBLE(crop, x - x0 + 10, y - y0 + 10) = PIXEL(image, x, y) - MEDIAN(realtime->avg, x, y);

        image_dump_to_fits(crop, "out_crop.r.fits");

        foreach(pixel, region->pixels){
            dprintf("%d %d %g %g %g\n", pixel->x, pixel->y, pixel->excess, pixel->flux, pixel->flux_err);
        }

        dprintf("center at: %g %g\n", record->x - x0, record->y - y0);

        exit(1);
    }

    /* Time and frame number */
    record->time = image->time;

    /* Sky coords */
    coords_get_ra_dec(&realtime->coords, record->x, record->y, &record->ra, &record->dec);

    /* Magnitude */
    if(record->flux > 0){
        record->mag = coords_get_mag(&realtime->coords, -2.5*log10(record->flux), record->x, record->y, &record->mag_err);
        record->mag_err = hypot(record->mag_err, 2.5*log10(1 + record->flux_err/record->flux));
    } else {
        /* Fake magnitude for negative fluxes */
        record->mag = 100.0;
        record->mag_err = 0;
    }

    return record;
}

int region_is_noise(average_image_str *avg, image_str *image, image_str *dark, region_str *region)
{
    int result = FALSE;
    pixel_str *pixel = list_first_item(region->pixels);
    int x0 = pixel->x;
    int y0 = pixel->y;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    int x3 = 0;
    int y3 = 0;

    /* return FALSE; */

    get_reachable_maximum_xy(avg, image, x0, y0, &x0, &y0, 0, 0); /* Local peak in excess image */
    get_reachable_maximum_xy(avg, image, x0, y0, &x1, &y1, 2, 4); /* Local peak in sigma image */

    get_reachable_maximum_xy(avg, image, x0, y0, &x2, &y2, 1, 8); /* Local peak in median image */
    get_reachable_minimum_xy(avg, image, x0, y0, &x3, &y3, 1, 8); /* Local minimum in median image */

    if(MAD((avg), x0, y0) > 0 && MAD(avg, x1, y1) > 0){
        /* Excess of the peak computed using local maxima of MAD to account for bright/moving objects */
        double peak_excess = (PIXEL(image, x0, y0) - MEDIAN(avg, x0, y0))*1.0/MAD(avg, x1, y1);
        double median_diff = MEDIAN(avg, x2, y2) - MEDIAN(avg, x3, y3);

        if(dark)
            median_diff -= PIXEL_DOUBLE(dark, x2, y2) - PIXEL_DOUBLE(dark, x3, y3);

        if(peak_excess < 4)
            result = TRUE;

        /* if(!result && MAD(avg, x1, y1) > 3.0*MAD(avg, x0, y0)) */
        /*     result = TRUE; */

        if(!result && median_diff > 5.0*MAD(avg, x3, y3))
            result = TRUE;

        /* Filter small area events - our PSF is large */
        if(!result && list_length(&region->pixels) < 4)
            result = TRUE;

        /* Filter events just after average reset */
        if(!result && avg->median_length < 2.0*avg->nominal_length)
            result = TRUE;

        /* if(!result || TRUE ){ */
        /*     dprintf("%d %d -> %d %d: %g -> %g %s\n", x0, y0, x1, y1, EXCESS(avg, image, x0, y0), peak_excess, result ? "NOISE" : "NOT NOISE"); */
        /*     dprintf("diff=%g, MAD0 = %g, MAD=%g\n", PIXEL(image, x0, y0) - MEDIAN(avg, x0, y0), MAD(avg, x0, y0), MAD(avg, x1, y1)); */
        /*     dprintf("median diff=%g / %g: %d %d %g - %d %d %g\n", median_diff, median_diff/5.0/MAD(avg,x3,y3), x2, y2, MEDIAN(avg, x2, y2), x3, y3, MEDIAN(avg, x3, y3)); */
        /*     dprintf("%d pixels\n", list_length(&region->pixels)); */
        /*     dprintf("\n"); */
        /* } */
    }

    return result;
}

double get_median_diff(realtime_str *realtime, image_str *image, image_str *dark, int x0, int y0)
{
    average_image_str *avg = realtime->avg;
    int x2 = 0;
    int y2 = 0;
    int x3 = 0;
    int y3 = 0;

    get_reachable_maximum_xy(avg, image, x0, y0, &x2, &y2, 1, 4); /* Local peak in median image */
    get_reachable_minimum_xy(avg, image, x0, y0, &x3, &y3, 1, 4); /* Local minimum in median image */

    return (MEDIAN(avg, x2, y2) - MEDIAN(avg, x3, y3))/3.0*MAD(avg, x3, y3);
}

/* Warning: time-critical function! */
void realtime_find_records(realtime_str *realtime, image_str *image)
{
    struct list_head regions;
    int npixels = image->width*image->height;
    region_str **mask = (region_str **)calloc(npixels, sizeof(record_str *));
    int d;

    init_list(regions);
    init_list(realtime->records);

    madvise(mask, npixels*sizeof(record_str *), MADV_SEQUENTIAL);

    for(d = 0; d < image->width*image->height; d++){
        /* FIXME: hand-made optimization */
        //double excess = image->data[d] - AVGSCALE*realtime->avg->median[2*d];

        //excess /= AVGSCALE*realtime->avg->median[2*d + 1];
        /* On zero MAD it will set the result to INF, and we will catch it by isfinite() later */

        /* SSE stuff - prefetch data */
        /* _mm_prefetch((char *)&image->data[d+16], _MM_HINT_NTA);; */
        /* _mm_prefetch((char *)&realtime->avg->median[2*d+16], _MM_HINT_NTA);; */

        /* Initial point for region extraction.  Threshold is chosen more
           or less arbitrarily */
        //if(excess > 10 && isfinite(excess) && !mask[d] && !(realtime->badpix && realtime->badpix->data[d])){

        int excess = image->data[d]*INV_AVGSCALE_INT - realtime->avg->median[2*d];

        if(excess > 4*realtime->avg->median[2*d + 1] && realtime->avg->median[2*d + 1] > 0 &&
           !mask[d] && !(realtime->badpix && realtime->badpix->data[d])){

            int y0 = floor(d/image->width);
            int x0 = d - y0*image->width;

            /* The idea is to find locally reachable maximum, and then
               recursively claim all points reachable by descending. The
               maximum should be free, as in other case the initial point
               should be already claimed. */
            get_reachable_maximum_xy(realtime->avg, image, x0, y0, &x0, &y0, 0, 0);

            /* This point inevitaby has the excess > 4. Don't need to check. */
            /* It also is not claimed -- otherwise our initial point would be too */
            if(!mask[image->width*y0 + x0]){
                region_str *region = extract_region(realtime, image, mask, x0, y0);

                /* extract_region may eat some regions extracted before. So
                   we will not process regions here, but just store them
                   for now */
                add_to_list(regions, region);
            }
        }
    }

    /* Now process all regions */
    {
        region_str *region = NULL;

        /* Throw away single-pixel events - they are too many */
        foreach(region, regions)
            if(region->npixels <= 1 || region->npixels_significant <= 1 || region->replaced){
                free_list(region->pixels);
                del_from_list_in_foreach_and_run(region, free(region));
            } else if(region_is_noise(realtime->avg, image, realtime->dark, region))
                region->is_noise = TRUE;

        foreach(region, regions){
            record_str *record = record_from_region(realtime, region, image);

            region->record = record;

            /* Filter out truncated and not elongated records */
            if(record->flags & RECORD_TRUNCATED && !(record->flags & RECORD_ELONGATED)){
                free(record);
                free_list(region->pixels);
                del_from_list_in_foreach_and_run(region, free(region));
            } else
                add_to_list(realtime->records, record);
        }

        /* Special treatment of elongated regions - meteor trails */
        /* TODO: catch the trails of alreagy faded meteors, when there are no
           elongated records, but a lot of "usual" ones along the trail */
        foreach(region, regions){
            if(region->record->flags & RECORD_ELONGATED){
                /* Find and merge unconnected regions located along the same trail */
                region_str *r = NULL;
                double sin_theta = sin(region->record->theta);
                double cos_theta = cos(region->record->theta);

                foreach(r, regions)
                    if(r != region){
                        double dist_normal = fabs(sin_theta*(r->record->x - region->record->x) - cos_theta*(r->record->y - region->record->y));
                        double dist = hypot(r->record->x - region->record->x, r->record->y - region->record->y);

                        int need_merge = FALSE;

                        if(r->record->flags & RECORD_ELONGATED){
                            /* Record is elongated too. Check the angle then,
                               if it is not truncated. Distance constraint is
                               not so tight in this case. */
                            double delta_theta = 180./M_PI*acos(cos(r->record->theta)*cos_theta + sin(r->record->theta)*sin_theta);

                            if((fabs(delta_theta) < realtime->max_meteor_angle ||
                                r->record->flags & RECORD_TRUNCATED) &&
                               dist_normal*dist_normal < 3*(region->record->b*region->record->b + r->record->b*r->record->b))
                                need_merge = TRUE;
                        } else {
                            /* Ordinary record. Check the distance from the trail. */
                            if(dist_normal*dist_normal < region->record->b*region->record->b +
                               r->record->a*r->record->a + r->record->b*r->record->b &&
                               /* Also check linear distance - it should not be too large */
                               dist < 3.0*hypot(r->record->a, region->record->a))
                                need_merge = TRUE;
                        }

                        if(need_merge){
                            double delta_theta = 180./M_PI*acos(cos(r->record->theta)*cos_theta + sin(r->record->theta)*sin_theta);
                            /* dprintf("Merging: %g %g %g %g %g %d into %g %g %g %g %g %d\n", r->record->x, r->record->y, r->record->a, r->record->b, r->record->theta, r->record->flags, */
                            /*         region->record->x, region->record->y, region->record->a, region->record->b, region->record->theta, region->record->flags); */
                            /* dprintf("delta_theta=%g, dist_normal=%g dist=%g\n", delta_theta, dist_normal, dist); */

                            add_list_to_list(region->pixels, r->pixels);

                            region->npixels += r->npixels;

                            del_from_list(r->record);
                            free(r->record);

                            del_from_list(region->record);
                            free(region->record);

                            region->record = record_from_region(realtime, region, image);
                            region->record->flags |= RECORD_MERGED;
                            add_to_list(realtime->records, region->record);

                            /* After this statement r is no longer the same as before */
                            del_from_list_in_foreach_and_run(r, free(r));
                        }
                    }
            }
        }

        foreach(region, regions)
            free_list(region->pixels);
    }

    free_list(regions);
    free(mask);
}
