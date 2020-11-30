#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <glib.h>
#include <string.h>

#include "utils.h"

#include "andor.h"

/* We will reset the clock after this amount of frames */
#define RESET_COUNTER_LIMIT 1000
#define RESET_SECONDS 100
static int reset_counter = 0;
static time_str reset_timestamp;

/* #define USE_STRIDE */

/* #define ANDOR_DEBUG */

static inline double current_time(void) {
   struct timeval t;
   gettimeofday(&t,NULL);
   return (double) t.tv_sec + t.tv_usec/1000000.0;
}

static int just_reset_time = FALSE;

/* Reset the internal clocks */
void andor_reset_clock(andor_str *andor)
{
    double t0 = 0;
    double t1 = 0;

    do {
        t0 = current_time();

        andor->time0 = time_current();
        AT_Command(andor->handle, L"TimestampClockReset");

        t1 = current_time();

        dprintf("Resetting clock: %g\n", t1 - t0);
    } while(t1 - t0 > 0.002); /* Only succeed if the resetting is faster than 2 ms */

    reset_counter = 0;
    reset_timestamp = time_current();
}

andor_str *andor_create()
{
    andor_str *andor = NULL;
    AT_64 ndevices = 0;

    if(AT_InitialiseLibrary() != AT_SUCCESS){
        dprintf("Can't initialize ANDOR library!\n");
        return NULL;
    }

    AT_GetInt(AT_HANDLE_SYSTEM, L"Device Count", &ndevices);
    if(!ndevices){
        dprintf("No ANDOR devices found!");
        return NULL;
    } else
        dprintf("%lld ANDOR devices found\n", ndevices);

    andor = (andor_str *)malloc(sizeof(andor_str));

    /* Crude detection of simulated camera */
    andor->is_simcam = (ndevices == 2);

    AT_Open(0, &andor->handle);

    if(!andor->is_simcam){
        const wchar_t *model = get_string(andor->handle, L"CameraModel");

        if(wcscmp(model, L"DC-152Q-C00-FI") != 0){
            dprintf("Wrong camera model!\n");

            andor->is_simcam = TRUE;
        }
    }

    AT_SetBool(andor->handle, L"Overlap", 1);
    AT_SetEnumString(andor->handle, L"PixelEncoding", L"Mono16");
    AT_SetEnumString(andor->handle, L"TriggerMode", L"Internal");
    AT_SetEnumString(andor->handle, L"CycleMode", L"Continuous");
    AT_SetEnumIndex(andor->handle, L"PixelReadoutRate", 1);
    AT_SetEnumString(andor->handle, L"ElectronicShutteringMode", L"Rolling");
    AT_SetBool(andor->handle, L"SensorCooling", 1);
    AT_SetBool(andor->handle, L"MetadataEnable", 1);
    AT_SetBool(andor->handle, L"MetadataTimestamp", 1);
    AT_SetBool(andor->handle, L"SpuriousNoiseFilter", 0);
    AT_SetEnumIndex(andor->handle, L"AOIBinning", 0);
    AT_SetEnumIndex(andor->handle, L"SimplePreAmpGainControl", 2);
    AT_SetFloat(andor->handle, L"ExposureTime", 0.1);
    AT_SetFloat(andor->handle, L"TargetSensorTemperature", -20);

    /* List of buffers to clean them up */
    andor->chunks = g_hash_table_new_full(g_direct_hash, g_direct_equal, free, NULL);

    andor_reset_clock(andor);

    return andor;
}

void andor_cleanup(andor_str *andor)
{
    /* Clean up the unused buffers */
    g_hash_table_remove_all(andor->chunks);
}

void andor_delete(andor_str *andor)
{
    if(!andor)
        return;

    AT_Close(andor->handle);

    g_hash_table_destroy(andor->chunks);

    AT_FinaliseLibrary();

    free(andor);
}

AT_64 get_int(AT_H handle, const wchar_t *name)
{
    AT_64 value = 0;
    int i = 0;

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetInt(handle, name, &value);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return value;
}

AT_BOOL get_bool(AT_H handle, const wchar_t *name)
{
    AT_BOOL value = AT_FALSE;
    int i = 0;

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetBool(handle, name, &value);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return value;
}

double get_float(AT_H handle, const wchar_t *name)
{
    double value = 0;
    int i = 0;

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetFloat(handle, name, &value);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return value;
}

static double get_float_min(AT_H handle, const wchar_t *name)
{
    double value = 0;
    int i = 0;

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetFloatMin(handle, name, &value);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return value;
}

static double get_float_max(AT_H handle, const wchar_t *name)
{
    double value = 0;
    int i = 0;

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetFloatMax(handle, name, &value);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return value;
}

static char *w2c(AT_WC *string)
{
    static char buf[512];
    mbstate_t mbs;

    memset(&mbs, 0, sizeof(mbstate_t));

    mbrlen(NULL,0,&mbs);
    wcsrtombs(buf, &string, 512, &mbs);
    /* Just in case */
    buf[511] = '\0';

    return buf;
}

AT_WC *get_string(AT_H handle, const wchar_t *name)
{
    static AT_WC buffer[128];
    int i = 0;

    buffer[0] = L'\0';

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetString(handle, name, buffer, 128);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return buffer;
}

int get_enum_index(AT_H handle, const wchar_t *name)
{
    int i = 0;
    int index = -1;

    AT_IsImplemented(handle, name, &i);

    if(i){
        AT_GetEnumIndex(handle, name, &index);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return index;
}

AT_WC *get_enum_string(AT_H handle, const wchar_t *name)
{
    static AT_WC buffer[128];
    int i = 0;

    buffer[0] = L'\0';

    AT_IsImplemented(handle, name, &i);

    if(i){
        int ival = 0;

        AT_GetEnumIndex(handle, name, &ival);
        AT_GetEnumStringByIndex(handle, name, ival, buffer, 128);
    } else {
#ifdef ANDOR_DEBUG
        dprintf("Error: %ls not implemented!\n", name);
#endif
    }

    return buffer;
}

void andor_info(andor_str *andor)
{
    dprintf("Model: %ls\n", get_string(andor->handle, L"CameraModel"));
    dprintf("SN: %ls\n", get_string(andor->handle, L"SerialNumber"));

    dprintf("Sensor: %lld x %lld\n", get_int(andor->handle, L"SensorWidth"), get_int(andor->handle, L"SensorHeight"));
    dprintf("Binning: %ls\n", get_enum_string(andor->handle, L"AOIBinning"));
    dprintf("Area of Interest: %lld x %lld\n", get_int(andor->handle, L"AOIWidth"), get_int(andor->handle, L"AOIHeight"));
    dprintf("Temperature: %g\n", get_float(andor->handle, L"SensorTemperature"));
    dprintf("Sensor Cooling: %d\n", get_bool(andor->handle, L"SensorCooling"));

    dprintf("Timestamp: %lld\n", get_int(andor->handle, L"TimestampClock"));
    dprintf("TimestampClockFrequency: %lld\n", get_int(andor->handle, L"TimestampClockFrequency"));

    dprintf("Trigger Mode: %ls\n", get_enum_string(andor->handle, L"TriggerMode"));
    dprintf("Cycle Mode: %ls\n", get_enum_string(andor->handle, L"CycleMode"));
    dprintf("Shutter: %ls\n", get_enum_string(andor->handle, L"ElectronicShutteringMode"));

    dprintf("Exposure: %g s (%g - %g)\n", get_float(andor->handle, L"ExposureTime"),
            get_float_min(andor->handle, L"ExposureTime"), get_float_max(andor->handle, L"ExposureTime"));

    dprintf("Actual Exposure Time: %g\n", get_float(andor->handle, L"ActualExposureTime"));

    dprintf("ReadoutTime: %g\n", get_float(andor->handle, L"ReadoutTime"));

    dprintf("ReadoutRate: %ls\n", get_enum_string(andor->handle, L"PixelReadoutRate"));

    dprintf("Overlap: %d\n", get_bool(andor->handle, L"Overlap"));

    dprintf("Frame Rate: %g\n", get_float(andor->handle, L"Frame Rate"));
    dprintf("FrameCount: %lld\n", get_int(andor->handle, L"FrameCount"));

    dprintf("Pixel encoding: %ls\n", get_enum_string(andor->handle, L"PixelEncoding"));

    dprintf("PreAmpGainControl: %ls\n", get_enum_string(andor->handle, L"PreAmpGainControl"));
    dprintf("SimplePreAmpGainControl: %ls\n", get_enum_string(andor->handle, L"SimplePreAmpGainControl"));
    dprintf("PreAmpGainChannel: %ls\n", get_enum_string(andor->handle, L"PreAmpGainChannel"));
    AT_SetEnumString(andor->handle, L"PreAmpGainSelector", L"Low");
    dprintf("PreAmpGain Low: %ls\n", get_enum_string(andor->handle, L"PreAmpGain"));
    AT_SetEnumString(andor->handle, L"PreAmpGainSelector", L"High");
    dprintf("PreAmpGain High: %s\n", w2c(get_enum_string(andor->handle, L"PreAmpGain")));

    dprintf("Baseline: %lld\n", get_int(andor->handle, L"BaselineLevel"));

    dprintf("Image size (bytes): %lld\n", get_int(andor->handle, L"ImageSizeBytes"));

    dprintf("Metadata: %d\n", get_bool(andor->handle, L"MetadataEnable"));
    dprintf("MetadataTimestamp: %d\n", get_bool(andor->handle, L"MetadataTimestamp"));

    dprintf("SpuriousNoiseFilter: %d\n", get_bool(andor->handle, L"SpuriousNoiseFilter"));
    dprintf("StaticBlemishCorrection: %d\n", get_bool(andor->handle, L"StaticBlemishCorrection"));

    dprintf("Fan Speed: %ls\n", get_enum_string(andor->handle, L"FanSpeed"));
}

void andor_add_buffer(andor_str *andor)
{
    unsigned char *buf = NULL;
    AT_64 size = get_int(andor->handle, L"ImageSizeBytes");
    int i = 0;

    posix_memalign((void **)&buf, 8, size);

    i = AT_QueueBuffer(andor->handle, buf, size);

    if(i == AT_ERR_INVALIDHANDLE){
        /* Fallback */
        AT_Open(1, &andor->handle);
    }

    if(i != AT_SUCCESS){
        dprintf("Error %d\n", i);

        free(buf);
    }

    g_hash_table_insert(andor->chunks, buf, buf);
}

void andor_acquisition_start(andor_str *andor)
{
    int d;

    /* /\* Try to re-initialize the library and the camera, just in case *\/ */
    /* if(AT_InitialiseLibrary() != AT_SUCCESS){ */
    /*     dprintf("Can't initialize ANDOR library!\n"); */
    /*     return; */
    /* } */

    /* AT_Close(andor->handle); */
    /* AT_Open(0, &andor->handle); */

    /* Reset the internal clocks */
    andor_reset_clock(andor);

    for(d = 0; d < 10; d++)
        andor_add_buffer(andor);

    AT_Command(andor->handle, L"AcquisitionStart");
}

void andor_acquisition_stop(andor_str *andor)
{
    AT_Command(andor->handle, L"AcquisitionStop");
    AT_Flush(andor->handle);
    andor_cleanup(andor);
}

int andor_is_acquiring(andor_str *andor)
{
    return get_bool(andor->handle, L"CameraAcquiring");
}

void andor_cool(andor_str *andor)
{
    AT_SetBool(andor->handle, L"SensorCooling", 1);

    do {
        dprintf("Current temperature = %g, %ls\n",
                get_float(andor->handle, L"SensorTemperature"),
                get_enum_string(andor->handle, L"TemperatureStatus"));
        usleep(100000);
    } while(get_enum_index(andor->handle, L"TemperatureStatus") != 1);
}

image_str *andor_wait_image(andor_str *andor, double delay)
{
    image_str *image = NULL;
    unsigned char *buf = NULL;
    int size = 0;
    int i = 0;

    do {
        i = AT_WaitBuffer(andor->handle, &buf, &size, 1000*delay);
        if(i == AT_ERR_NODATA){
            dprintf("AT_ERR_NODATA\n");
            break;
        }
    } while(i != AT_SUCCESS && i != AT_ERR_TIMEDOUT && i != AT_ERR_INVALIDHANDLE);

    if(i == AT_ERR_NODATA){
        andor_acquisition_stop(andor);
        andor_acquisition_start(andor);
    }

    if(i != AT_SUCCESS)
        return NULL;

    /* Remove the buffer from cleanup list */
    g_hash_table_steal(andor->chunks, buf);

    andor_add_buffer(andor);

    /* Here we assume Mono16 packing */
#ifdef USE_STRIDE
    image = image_create_with_data(get_int(andor->handle, L"AOIStride")/2,
                                   get_int(andor->handle, L"AOIHeight"), (u_int16_t *)buf);
#else
    image = image_create(get_int(andor->handle, L"AOIWidth"),
                         get_int(andor->handle, L"AOIHeight"));

    {
        int stride = get_int(andor->handle, L"AOIStride");

        for(i = 0; i < image->height; i++)
            memcpy(image->data + image->width*i, buf + stride*i, 2*image->width);
    }
#endif

    image->time = time_current();

    image_keyword_add_double(image, "EXPOSURE", get_float(andor->handle, L"ExposureTime"), NULL);
    image_keyword_add_double(image, "FRAMERATE", get_float(andor->handle, L"FrameRate"), NULL);
    image_keyword_add_int(image, "SHUTTER", get_enum_index(andor->handle, L"ElectronicShutteringMode"), w2c(get_enum_string(andor->handle, L"ElectronicShutteringMode")));
    image_keyword_add_int(image, "OVERLAP", get_bool(andor->handle, L"Overlap"), "Overlap Readout Mode");
    image_keyword_add_int(image, "BINNING", get_enum_index(andor->handle, L"AOIBinning"), w2c(get_enum_string(andor->handle, L"AOIBinning")));
    image_keyword_add_int(image, "READOUTRATE", get_enum_index(andor->handle, L"PixelReadoutRate"), w2c(get_enum_string(andor->handle, L"PixelReadoutRate")));
    image_keyword_add_double(image, "READOUTTIME", get_float(andor->handle, L"ReadoutTime"), "Actual Readout Time");
    image_keyword_add_double(image, "TEMPERATURE", get_float(andor->handle, L"SensorTemperature"), "Sensor Temperature");
    image_keyword_add_int(image, "TEMPERATURESTATUS", get_enum_index(andor->handle, L"TemperatureStatus"), w2c(get_enum_string(andor->handle, L"TemperatureStatus")));
    image_keyword_add_int(image, "PIXELENCODING", get_enum_index(andor->handle, L"PixelEncoding"), w2c(get_enum_string(andor->handle, L"PixelEncoding")));
    image_keyword_add_int(image, "READOUTRATE", get_enum_index(andor->handle, L"PixelReadoutRate"), w2c(get_enum_string(andor->handle, L"PixelReadoutRate")));
    image_keyword_add_int(image, "PREAMP", get_enum_index(andor->handle, L"PreAmpGainControl"), w2c(get_enum_string(andor->handle, L"PreAmpGainControl")));
    image_keyword_add_int(image, "SIMPLEPREAMP", get_enum_index(andor->handle, L"SimplePreAmpGainControl"), w2c(get_enum_string(andor->handle, L"SimplePreAmpGainControl")));
    image_keyword_add_int(image, "NOISEFILTER", get_bool(andor->handle, L"SpuriousNoiseFilter"), "Spurious Noise Filter");
    image_keyword_add_int(image, "BASELINE", get_int(andor->handle, L"BaselineLevel"), "Current Baseline Level");
    image_keyword_add_int(image, "BLEMISHCORRECTION", get_bool(andor->handle, L"StaticBlemishCorrection"), "Statis Blemish Correction");

    if(get_bool(andor->handle, L"MetadataEnable")){
        unsigned char *ptr = buf + size;
        /* FIXME: check whether CID is 1 */
        /* int length = *(int *)(ptr - 4); */
        /* int cid = *(int *)(ptr - 8); */
        u_int64_t clock = *(u_int64_t *)(ptr - 16);
        int frequency = get_int(andor->handle, L"TimestampClockFrequency");

        image->time = time_incremented(andor->time0, 1.0*clock/frequency);
    } else
        image->time = time_current();

    /* Reset the grabber if time is strange */
    if(time_interval(image->time, time_current()) > 1e3*1e3 ||
       time_interval(image->time, time_current()) < -1e3*1e3){

        if(just_reset_time){
            //dprintf("Strange time: %g\n", 1e-3*time_interval(image->time, time_current()));
            dprintf("Resetting the grabber twice - mark it as SIMCAM\n");
            andor->is_simcam = TRUE;
        }

        andor_acquisition_stop(andor);
        andor_acquisition_start(andor);

        image_delete(image);

        just_reset_time = TRUE;
        return NULL;
    } else
        just_reset_time = FALSE;

    /* Reset the clock if necessary */
    if(reset_counter++ > RESET_COUNTER_LIMIT || time_interval(reset_timestamp, time_current()) > 1e3*RESET_SECONDS){
        reset_counter = 0;
        reset_timestamp = time_current();

        AT_Command(andor->handle, L"AcquisitionStop");
        AT_Flush(andor->handle);
        andor_cleanup(andor);

        andor_reset_clock(andor);

        AT_Command(andor->handle, L"AcquisitionStart");
        andor_add_buffer(andor);
    }

#ifndef USE_STRIDE
    /* We should free() the buffer if we did not reuse it for the image */
    free(buf);
#endif

    if(reset_counter == 1 && get_float(andor->handle, L"ExposureTime") < 0.2){
        /* Throw away first frame, as it is corrupted in global shutter */
        image_delete_and_null(image);
    }

    return image;
}

image_str *andor_get_image(andor_str *andor)
{
    return andor_wait_image(andor, 0);
}
