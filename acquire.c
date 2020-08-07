#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <wchar.h>
#include <sys/stat.h>

#include <atcore.h>

#include "utils.h"

#include "andor.h"
#include "image.h"

int main(int argc, char **argv)
{
    char *mask_filename = NULL;
    char *mean_filename = NULL;
    char *sigma_filename = NULL;
    andor_str *andor = NULL;
    double exposure = -1;
    double fps = -1;
    int length = 0;
    int binning = -1;
    int rate = -1;
    int shutter = -1;
    int preamp = -1;
    int is_cool = FALSE;
    int filter = -1;
    int overlap = -1;
    int d;

    int is_mask = FALSE;
    double threshold = 3;

    parse_args(argc, argv,
               "mask=%s", &mask_filename,
               "mean=%s", &mean_filename,
               "sigma=%s", &sigma_filename,
               "length=%d", &length,
               "exposure=%lf", &exposure,
               "fps=%lf", &fps,
               "binning=%d", &binning,
               "rate=%d", &rate,
               "shutter=%d", &shutter,
               "preamp=%d", &preamp,
               "filter=%d", &filter,
               "overlap=%d", &overlap,
               "-cool", &is_cool,

               "-mask", &is_mask,
               "threshold=%lf", &threshold,
               NULL);

    andor = andor_create();

    if(exposure > 0)
        AT_SetFloat(andor->handle, L"ExposureTime", exposure);
    if(fps > 0)
        AT_SetFloat(andor->handle, L"FrameRate", fps);
    if(binning >= 0)
        AT_SetEnumIndex(andor->handle, L"AOIBinning", binning);
    if(rate >= 0)
        AT_SetEnumIndex(andor->handle, L"PixelReadoutRate", rate);
    if(shutter >= 0)
        AT_SetEnumIndex(andor->handle, L"ElectronicShutteringMode", shutter);
    if(preamp >= 0){
        AT_SetEnumIndex(andor->handle, L"SimplePreAmpGainControl", preamp);
        if(preamp < 2)
            AT_SetEnumString(andor->handle, L"PixelEncoding", L"Mono12");
    }
    if(filter >= 0)
        AT_SetBool(andor->handle, L"SpuriousNoiseFilter", filter);
    if(overlap >= 0)
        AT_SetBool(andor->handle, L"Overlap", overlap);

    if(andor->is_simcam)
        return EXIT_FAILURE;

    andor_info(andor);

    if(is_cool)
        andor_cool(andor);

    andor_acquisition_start(andor);

    if(is_mask){
        double *sum = NULL;
        double *sum2 = NULL;
        int width = 0;
        int height = 0;
        int N = 0;
        int count = 0;
        image_str *mask_image = NULL;
        image_str *mean_image = NULL;
        image_str *sigma_image = NULL;

        dprintf("Acquiring noise mask with threshold=%g\n", threshold);

        /* Acquire sums */
        for(d = 0; d < length; d++){
            image_str *image = andor_wait_image(andor, 10);
            int i;

            if(!sum){
                width = image->width;
                height = image->height;
                N = width*height;
                dprintf("Image size: %d x %d\n", width, height);

                sum = calloc(width*height, sizeof(double));
                sum2 = calloc(width*height, sizeof(double));

                mask_image = image_create(width, height);
                mean_image = image_create_double(width, height);
                sigma_image = image_create_double(width, height);

                image_copy_properties(image, mask_image);
                image_copy_properties(image, mean_image);
                image_copy_properties(image, sigma_image);

                image_keyword_add(mask_image, "IMAGETYPE", "mask", "0 for good, 1 for bad pixels");
            }

            for(i = 0; i < width*height; i++){
                sum[i] += image->data[i];
                sum2[i] += image->data[i]*image->data[i];
            }

            dprintf("\t%d / %d\r", d, length);

            image_delete(image);
        }

        /* Compute mask */
        for(d = 0; d < width*height; d++){
            double mean = sum[d]*1./length;
            double sigma = (sum2[d] - sum[d]*sum[d]/length)/(length - 1);

            sigma = sqrt(sigma);

            if(sigma > threshold){
                count ++;

                mask_image->data[d] = 1;
            } else
                mask_image->data[d] = 0;

            mean_image->double_data[d] = mean;
            sigma_image->double_data[d] = sigma;
        }

        dprintf("%d (%.3g%%) pixels above threshold\n", count, 100.0*count/N);

        if(mask_filename){
            image_dump_to_fits(mask_image, mask_filename);
            dprintf("Mask stored to %s\n", mask_filename);
        }

        if(mean_filename){
            image_dump_to_fits(mean_image, mean_filename);
            dprintf("Mean stored to %s\n", mean_filename);
        }

        if(sigma_filename){
            image_dump_to_fits(sigma_image, sigma_filename);
            dprintf("Sigma stored to %s\n", sigma_filename);
        }

        image_delete(mask_image);
        image_delete(mean_image);
        image_delete(sigma_image);

        free(sum);
        free(sum2);
    }

    andor_acquisition_stop(andor);

    andor_delete(andor);

    return EXIT_SUCCESS;
}
