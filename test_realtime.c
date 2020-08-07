#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "utils.h"
#include "realtime.h"
#include "realtime_types.h"
#include "realtime_predict.h"
#include "dataserver.h"
#include "simulator.h"
#include "random.h"

int is_ds9 = FALSE;

static int is_flash = FALSE;
static int flash_x = 158.5;
static int flash_y = 261.6;

static int is_dump = FALSE;

static time_str time0 = {1900, 1, 1, 0, 0, 0, 0};

static void ds9_set_filename(char *filename)
{
    char *command = make_string("xpaset -p ds9 file fits %s", filename);

    system(command);

    free(command);
}

void dump_records_to_stdout(realtime_str *realtime, void *data)
{
    record_str *record = NULL;

    if(!is_flash)
        return;

    foreach(record, realtime->records){
        if(hypot(record->x - flash_x, record->y - flash_y) < 10){
            /* if(time_is_zero(time0)) */
            /*     time0 = record->time; */

            printf("%g %g %g %g %g\n",
                   1e-3*time_interval(time0, record->time),
                   record->x, record->y, record->sigma_x, record->sigma_y);

        }
    }
}

void dump_records(realtime_str *realtime, void *data)
{
    FILE *proc = NULL;
    record_str *record = NULL;
    int need_sleep = FALSE;

    if(is_ds9)
        proc = popen("xpaset ds9 regions", "w");

    foreach(record, realtime->records){
        if(proc)
            fprintf(proc, "ellipse(%g, %g, %g, %g, %g)\n",
                    record->x + 1, record->y + 1, record->a, record->b, record->theta*180./M_PI);
    }

    if(proc)
        pclose(proc);
}

void dump_objects(realtime_str *realtime, void *data)
{
    FILE *proc = NULL;
    object_str *object;
    char *timestamp = time_str_get_date_time(realtime->time);
    int number = 0;
    int number_single = 0;
    int number_double = 0;
    int number_checking = 0;
    int number_moving = 0;
    int number_meteor = 0;
    int number_flashes = 0;

    if(is_ds9)
        proc = popen("xpaset ds9 regions", "w");

    foreach(object, realtime->objects){
        record_str *record = NULL;
        record_str *record_prev = NULL;
        record_str *record0 = (record_str *)(list_last_item(object->records));

        if(proc && object->state != OBJECT_SINGLE && object->state != OBJECT_DOUBLE && object->state != OBJECT_CHECKING){
            fprintf(proc, "text %g %g # text={%d - %Ld}\n", record0->x + 20, record0->y + 10, object->state, object->id);

            foreach(record, object->records){
                fprintf(proc, "ellipse(%g, %g, %g, %g, %g)\n",
                        record->x + 1, record->y + 1, record->a, record->b, record->theta*180./M_PI);
                if(record_prev){
                    fprintf(proc, "line(%g, %g, %g, %g)\n",
                            record_prev->x + 1, record_prev->y + 1, record->x + 1, record->y + 1);
                }
                record_prev = record;
            }
        }

        if(hypot(object->Cx[0] - flash_x, object->Cy[0] - flash_y) < 10 && is_flash){
            foreach(record, object->records){
                dprintf("%g %g %g %g %g\n",
                        1e-3*time_interval(time0, record->time),
                        record->x, record->y, record->sigma_x, record->sigma_y);
            }

            dprintf("\n");
        }

        number ++;

        if(object->state == OBJECT_SINGLE)
            number_single ++;
        if(object->state == OBJECT_DOUBLE)
            number_double ++;
        if(object->state == OBJECT_CHECKING)
            number_checking ++;
        if(object->state == OBJECT_MOVING)
            number_moving ++;
        if(object->state == OBJECT_METEOR)
            number_meteor ++;
        if(object->state == OBJECT_FLASH)
            number_flashes ++;
    }

    dprintf("%4d objects (S: %4d D: %4d C: %4d m: %4d M: %4d F:%4d) at %s - %g\n",
            number, number_single, number_double, number_checking, number_moving, number_meteor, number_flashes,
            timestamp, 1e-3*time_interval(time0, realtime->time));

    free(timestamp);

    if(proc)
        pclose(proc);
}

void dump_object_records(realtime_str *realtime, object_str *object, void *data)
{
    FILE *proc = NULL;
    record_str *record0 = (record_str *)list_first_item(object->records);
    record_str *record = NULL;
    int need_sleep = TRUE;

    if(object->state == OBJECT_SINGLE || object->state == OBJECT_DOUBLE || object->state == OBJECT_CHECKING)
        return;

    if(is_ds9)
        proc = popen("xpaset ds9 regions", "w");

    dprintf("Object %lld (state=%d) had %d records\n", object->id, object->state, list_length(&object->records));

    if(proc){
        record_str *record0 = (record_str *)(list_last_item(object->records));

        fprintf(proc, "text %g %g # text={%d - %Ld}\n", record0->x + 20, record0->y + 10, object->state, object->id);

        foreach(record, object->records){
            fprintf(proc, "ellipse(%g, %g, %g, %g, %g)\n",
                    record->x + 1, record->y + 1, record->a, record->b, record->theta*180./M_PI);
            need_sleep = TRUE;
        }
    }

    {
        FILE *out = fopen("out.txt", "w");
        foreach(record, object->records){
            fprintf(out, "%g %g %g %g %g\n",
                    1e-3*time_interval(time0, record->time),
                    record->x, record->y, record->sigma_x, record->sigma_y);
        }
        fclose(out);

        out = fopen("out.1.txt", "w");
        object_str *object1 = NULL;

        foreach(object1, realtime->objects)
            if(object1->state == 3){
                foreach(record, object1->records){
                    fprintf(out, "%g %g %g %g %g\n",
                            1e-3*time_interval(time0, record->time),
                            record->x, record->y, record->sigma_x, record->sigma_y);
                }

                fprintf(out, "\n");
        }
        fclose(out);

    }

    /* if(object->id != 4591) */
    /*     exit(1); */

    if(proc)
        pclose(proc);

    /* if(object->state != OBJECT_METEOR && object->state != OBJECT_MOVING /\* && object->state != OBJECT_FLASH *\/) */
        /* exit(1); */

    /* sleep(5); */
}

void alert_start(realtime_str *realtime, object_str *object, void *data)
{
    record_str *record0 = (record_str *)list_first_item(object->records);
    record_str *r = (record_str *)list_last_item(object->records);
    record_str *record = NULL;

    dprintf("Alert for object %Ld at %g %g at length %d\n", object->id, record0->x, record0->y, object->length);

    foreach(record, object->records){
        dprintf("%g %g %g %g %g - %g - %d\n",
                1e-3*time_interval(record0->time, record->time),
                record->x, record->y, record->sigma_x, record->sigma_y, record->excess, record->flags);
    }
    dprintf("\n");

    is_dump = TRUE;

    //exit(1);
}

void alert_cancel(realtime_str *realtime, object_str *object, void *data)
{
    record_str *record0 = (record_str *)list_first_item(object->records);
    record_str *r = (record_str *)list_last_item(object->records);
    record_str *record = NULL;

    dprintf("Alert for object %Ld at %g %g cancelled at length %d\n", object->id, record0->x, record0->y, object->length);

    foreach(record, object->records){
        dprintf("%g %g %g %g %g\n",
                1e-3*time_interval(record0->time, record->time),
                record->x, record->y, record->sigma_x, record->sigma_y);
    }
    dprintf("\n");
}

void alert_finish(realtime_str *realtime, object_str *object, void *data)
{
    record_str *record0 = (record_str *)list_first_item(object->records);
    record_str *record = NULL;

    if(object->state != OBJECT_MOVING && object->state != OBJECT_METEOR)
        dprintf("Alert for object %Ld at %g %g finished at length %d\n", object->id, record0->x, record0->y, object->length);

    foreach(record, object->records){
        dprintf("%g %g %g %g %g\n",
                1e-3*time_interval(record0->time, record->time),
                record->x, record->y, record->sigma_x, record->sigma_y);
    }
    dprintf("\n");
    exit(1);
}

int main(int argc, char **argv)
{
    dataserver_str *server = NULL;
    char *base = "IMG";
    char *filename_out = "/tmp/out.fits";
    char *filename_avg = "/tmp/out_avg.fits";
    char *filename_median = "/tmp/out_median.fits";
    char *filename_sigma = "/tmp/out_sigma.fits";
    char *filename_excess = "/tmp/out_excess.fits";
    char *darkname = NULL;
    char *flatname = NULL;
    image_str *dark = NULL;
    image_str *flat = NULL;
    double ra0 = 266.4;
    double dec0 = -28.9;
    double pixscale = 25.9;
    simulator_str *sim = NULL;
    double sigma_correction = 1.0;
    int width = 600;
    int height = 600;
    int skip = 0;
    int x0 = 0;
    int y0 = 0;

    int is_sim = FALSE;
    int is_crop = FALSE;

    parse_args(argc, argv,
               "base=%s", &base,
               "ra0=%lf", &ra0,
               "dec0=%lf", &dec0,
               "pixscale=%lf", &pixscale,
               "skip=%d", &skip,
               "dark=%s", &darkname,
               "flat=%s", &flatname,
               "-ds9", &is_ds9,
               "-sim", &is_sim,
               "x0=%d", &x0,
               "y0=%d", &y0,
               "width=%d", &width,
               "height=%d", &height,
               "sigma_correction=%lf", &sigma_correction,
               "-crop", &is_crop,
               /* "%s", &filename_out, */
               /* "%s", &filename_avg, */
               /* "%s", &filename_sigma, */
               "%s", &base,
               NULL);

    if(darkname){
        dark = image_create_from_fits(darkname);
    }

    if(flatname){
        flat = image_create_from_fits(flatname);
    }

    if(is_sim){
        sim = simulator_create(width, height);

        sim->time = time_current();
        sim->saturation = 65535;

        sim->satellite_probability = 0.005;
        sim->meteor_probability = 0.00;
        sim->flash_probability = 0.02;

        simulator_set_field(sim, ra0, dec0, pixscale);

        time0 = sim->time;

        random_initialize(3);
    } else
        server = dataserver_create(base);

    if(server || sim){
        realtime_str *rt = realtime_create(100);
        image_str *image = NULL;
        u_int64_t frame0 = dataserver_frame_first(server);;
        u_int64_t frame_number = frame0 + skip;
        time_str time_prev = time_zero();

        rt->badpix = image_create_from_fits("mask.fits");
        rt->dark = dark;
        rt->flat = flat;
        rt->sigma_correction = sigma_correction;

        /* REALTIME_SET_HOOK(rt, records_extracted_hook, dump_records, NULL); */
        REALTIME_SET_HOOK(rt, objects_iterated_hook, dump_objects, NULL);
        /* REALTIME_SET_HOOK(rt, object_finished_hook, alert_finish, NULL); */
        REALTIME_SET_HOOK(rt, object_finished_hook, dump_object_records, NULL);
        REALTIME_SET_HOOK(rt, flash_detected_hook, alert_start, NULL);
        REALTIME_SET_HOOK(rt, flash_cancelled_hook, alert_cancel, NULL);

        rt->sigma_correction = 1.2;

        while(1){
            if(sim){
                image = simulator_get_image(sim);

                simulator_iterate(sim);
            } else {
                image = dataserver_get_frame(server, frame_number);

                if(time_is_zero(time0))
                    time0 = image->time;
            }

            if(image && is_crop){
                image_str *crop = image_crop(image, x0, y0, x0 + width, y0 + height);

                image_delete(image);
                image = crop;
            }

            if(image){
                image_str *aimage = NULL;//average_image_mean(rt->avg);
                image_str *mimage = NULL;//average_image_median(rt->avg);
                image_str *simage = NULL;//average_image_sigma(rt->avg);
                image_str *eimage = NULL;//average_image_excess_image(rt->avg, image);

                if(average_image_is_median_usable(rt->avg) && is_ds9)
                    eimage = average_image_excess_image(rt->avg, image);

                if(coords_is_empty(&rt->coords))
                    rt->coords = image->coords;

                //dprintf("Got image %Ld: %s, %s\n", frame_number - frame0, time_str_get_date_time(image->time), time_str_get_date_time(time_prev));
                /* dprintf("%g\n", get_dotP(image->time, -29.25, 70.73, ra0, dec0)); */

                if(!time_is_zero(time_prev) && time_interval(time_prev, image->time) > 1e3*5){
                    //exit_with_error("111");
                    dprintf("Time jump, resetting\n");
                    realtime_reset(rt);
                }

                time_prev = image->time;

                if(0)
                    image_dump_to_fits(image, filename_out);

                if(aimage){
                    image_dump_to_fits(aimage, filename_avg);
                    image_delete(aimage);
                }

                if(mimage){
                    image_dump_to_fits(mimage, filename_median);
                    image_delete(mimage);
                }

                if(simage){
                    image_dump_to_fits(simage, filename_sigma);
                    image_delete(simage);
                }

                if(eimage){
                    image_dump_to_fits(eimage, filename_excess);
                    image_delete(eimage);
                }

                //ds9_set_filename(filename_out);
                if(average_image_is_median_usable(rt->avg) && is_ds9)
                    ds9_set_filename(filename_excess);
                //usleep(1e6);

                realtime_process_frame(rt, image);

                if(is_dump || rt->avg->median_length > 8500){
                    image_str *median = average_image_median(rt->avg);

                    if(rt->dark)
                        image_combine(median, 1.0, rt->dark, IMAGE_OP_SUB);

                    image_dump_to_fits(average_image_excess_image(rt->avg, image), filename_excess);
                    image_dump_to_fits(average_image_sigma(rt->avg), filename_sigma);
                    image_dump_to_fits(median, filename_median);
                    image_dump_to_fits(image, filename_out);


                    {
                        int d;
                        FILE *file = fopen("/tmp/distr.txt", "w");

                        for(d = 0; d < 10000; d++){
                            int x0 = random_double()*(image->width-1);
                            int y0 = random_double()*(image->height-1);

                            fprintf(file, "%g\n", get_median_diff(rt, image, rt->dark, x0, y0));
                        }

                        fclose(file);
                    }

                    /*
                    realtime_find_records(rt, image);

                    FILE *f = fopen("/tmp/histogram.txt", "w");
                    int i;

                    for(i = 0; i < rt->histogram_length; i++)
                        fprintf(f, "%d %d\n", i, rt->histogram[i]);

                    fclose(f);
                    */

                    exit(1);
                }

                /* if(sim->frame_number > 100){ */
                /*     char *command = make_string("xpaset -p ds9 saveimage png video/%05d.png", sim->frame_number); */

                /*     system(command); */

                /*     free(command); */
                /* } */

                //image_dump_to_fits(image, "/tmp/out_zero.fits");

                if(average_image_is_median_usable(rt->avg) && 0){
                    //printf("%d %g %g %g\n", PIXEL(image, 516, 451), MEDIAN(rt->avg, 516, 451), MAD(rt->avg, 516, 451), EXCESS(rt->avg, image, 516, 451));
                    //printf("%d %g %g %g\n", PIXEL(image, 519, 455), MEDIAN(rt->avg, 519, 455), MAD(rt->avg, 519, 455), EXCESS(rt->avg, image, 519, 455));
                    printf("%d %g %g %g\n", PIXEL(image, 460, 466), MEDIAN(rt->avg, 460, 466), MAD(rt->avg, 460, 466), EXCESS(rt->avg, image, 460, 466));
                    fflush(stdout);
                }
                image_delete(image);
            } else
                break;

            frame_number ++;
        }

        realtime_delete(rt);

        if(sim)
            simulator_delete(sim);
        else
            dataserver_delete(server);
    }

    return EXIT_SUCCESS;
}
