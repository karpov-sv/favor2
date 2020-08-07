#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/stat.h>

#include <atcore.h>

#include "utils.h"

#include "andor.h"
#include "image.h"

static void ds9_set_filename(char *filename)
{
    system_run("xpaset -p ds9 file fits %s", filename);
}

int main(int argc, char **argv)
{
    char *base = "out";
    andor_str *andor = NULL;
    double exposure = -1;
    double fps = -1;
    int length = 0;
    int trigger = -1;
    int binning = -1;
    int rate = -1;
    int shutter = -1;
    int preamp = -1;
    int is_cool = FALSE;
    int filter = -1;
    int overlap = -1;
    int bias = -1;
    int is_ds9 = FALSE;
    int is_sum = FALSE;
    int d;

    parse_args(argc, argv,
               "base=%s", &base,
               "length=%d", &length,
               "exposure=%lf", &exposure,
               "fps=%lf", &fps,
               "trigger=%d", &trigger,
               "binning=%d", &binning,
               "rate=%d", &rate,
               "shutter=%d", &shutter,
               "preamp=%d", &preamp,
               "filter=%d", &filter,
               "overlap=%d", &overlap,
               "bias=%d", &bias,
               "-cool", &is_cool,
               "-ds9", &is_ds9,
               "-sum", &is_sum,
               NULL);

    andor = andor_create();

    if(trigger >= 0)
        AT_SetEnumIndex(andor->handle, L"TriggerMode", trigger);

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
    if(bias >= 0)
        AT_SetInt(andor->handle, L"BaselineLevel", bias);

    andor_info(andor);

    if(is_cool)
        andor_cool(andor);

    if(is_ds9){
        char *tmpname = make_string("/tmp/out-%s.fits", getlogin());

        while(TRUE){
            andor_acquisition_start(andor);
            image_str *image = andor_wait_image(andor, 10);

            image_dump_to_fits(image, tmpname);

            ds9_set_filename(tmpname);

            image_delete(image);

            andor_acquisition_stop(andor);
            usleep(500000);
        }
    }

    andor_acquisition_start(andor);

    mkdir(base, 0755);

    //sleep(5);

    if(is_sum){
        double *sum = NULL;
        double *sum2 = NULL;

        char *shutter = make_string("%ls", get_enum_string(andor->handle, L"ElectronicShutteringMode"));
        char *rate = make_string("%ls", get_enum_string(andor->handle, L"PixelReadoutRate"));
        char *enc = make_string("%ls", get_enum_string(andor->handle, L"PixelEncoding"));
        char *channel = make_string("%ls", get_enum_string(andor->handle, L"PreAmpGainChannel"));

        AT_SetEnumString(andor->handle, L"PreAmpGainSelector", L"Low");
        char *gain_low = make_string("%ls", get_enum_string(andor->handle, L"PreAmpGain"));

        AT_SetEnumString(andor->handle, L"PreAmpGainSelector", L"High");
        char *gain_high = make_string("%ls", get_enum_string(andor->handle, L"PreAmpGain"));

        char *bin = make_string("%ls", get_enum_string(andor->handle, L"AOIBinning"));

        char *filename = make_string("sums_exp_%g_%s_%s_preamp_%d_%s_%s_%s_%s_overlap_%d_filter_%d_%s.bin",
                                     get_float(andor->handle, L"ExposureTime"),
                                     shutter,
                                     rate,
                                     get_enum_index(andor->handle, L"SimplePreAmpGainControl"),
                                     enc,
                                     channel,
                                     gain_low,
                                     gain_high,
                                     get_bool(andor->handle, L"Overlap"),
                                     get_bool(andor->handle, L"SpuriousNoiseFilter"),
                                     bin
                                     );
        int width = 0;
        int height = 0;

        dprintf("Filename: %s\n", filename);

        /* Acquire sums */
        for(d = 0; d < length; d++){
            image_str *image = andor_wait_image(andor, 10);
            char *t = time_str_get_date_time(image->time);
            int i;

            dprintf("%s T=%g %d/%d\n", t, image_keyword_get_double(image, "TEMPERATURE"), d, length);

            if(!sum){
                width = image->width;
                height = image->height;
                dprintf("Image size: %d x %d\n", width, height);

                sum = calloc(width*height, sizeof(double));
                sum2 = calloc(width*height, sizeof(double));
            }

            for(i = 0; i < width*height; i++){
                sum[i] += image->data[i];
                sum2[i] += image->data[i]*image->data[i];
            }

            image_delete(image);
        }

        /* Write sums */
        {
            FILE *file = fopen(filename, "w");

            fwrite(sum, sizeof(double), width*height, file);
            fwrite(sum2, sizeof(double), width*height, file);

            fclose(file);
        }

        free(sum);
        free(sum2);
    } else for(d = 0; d < length; d++){
            image_str *image = andor_wait_image(andor, 10);
            char *filename = make_string("%s/%06d.fits", base, d);
            char *t = time_str_get_date_time(image->time);

            dprintf("%s T=%g %d/%d\n", t, image_keyword_get_double(image, "TEMPERATURE"), d, length);

            image_dump_to_fits(image, filename);

            free(t);
            free(filename);
            image_delete(image);
        }

    andor_acquisition_stop(andor);

    andor_delete(andor);

    return EXIT_SUCCESS;
}
