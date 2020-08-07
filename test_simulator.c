#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "utils.h"
#include "realtime.h"
#include "realtime_types.h"
#include "simulator.h"
#include "simulator_types.h"
#include "random.h"
#include "realtime_predict.h"

static void ds9_set_filename(char *filename)
{
    char *command = make_string("xpaset -p ds9 file fits %s", filename);
    //char *command = make_string("xpaset -p ds9 file fits %s && xpaset -p ds9 scale limits 100 1024", filename);
    // char *command = make_string("xpaset -p ds9 file fits %s && xpaset -p ds9 scale limits 0 400", filename);

    system(command);

    free(command);
}

void dump_records(realtime_str *realtime, void *data)
{
    FILE *proc = popen("xpaset ds9 regions", "w");
    record_str *record = NULL;
    int need_sleep = FALSE;

    if(!proc)
        exit(1);

    dprintf("%d records\n", list_length(&realtime->records));

    {
        object_str *object;
        char *timestamp = time_str_get_date_time(realtime->time);
        int number = 0;
        int number_single = 0;
        int number_double = 0;
        int number_moving = 0;
        int number_flashes = 0;
        int number_meteor = 0;

        foreach(object, realtime->objects){
            number ++;

            if(object->state == OBJECT_SINGLE)
                number_single ++;
            if(object->state == OBJECT_DOUBLE)
                number_double ++;
            if(object->state == OBJECT_MOVING)
                number_moving ++;
            if(object->state == OBJECT_FLASH)
                number_flashes ++;
            if(object->state == OBJECT_METEOR)
                number_meteor ++;
        }

        dprintf("%d objects (S: %d, D: %d, m: %d, M: %d, F:%d) at %s\n",
                number, number_single, number_double, number_moving, number_meteor, number_flashes, timestamp);

        free(timestamp);
    }

    foreach(record, realtime->records){
        if(record->flags & RECORD_ELONGATED){
            fprintf(proc, "ellipse(%g, %g, %g, %g, %g)\n",
                    record->x + 1, record->y + 1, record->a, record->b, record->theta*180./M_PI);
            //need_sleep = TRUE;
        }
    }

    pclose(proc);

    if(need_sleep)
        usleep(2e6);
}

void compare_flash_brightness(realtime_str *realtime, void *data)
{
    simulator_str *sim = (simulator_str *)data;
    simulated_flash_str *f = NULL;

    dprintf("%d events\n", list_length(&sim->events));

    foreach(f, sim->events)
        if(f->type == SIMULATED_FLASH){
            record_str *record = NULL;
            record_str *closest_record = NULL;
            double closest_dist = sim->width + sim->height;

            foreach(record, realtime->records){
                double dist = sqrt((record->x - f->x)*(record->x - f->x) + (record->y - f->y)*(record->y - f->y));

                if(dist < closest_dist){
                    closest_dist = dist;
                    closest_record = record;
                }
            }

            if(closest_record && closest_dist < sim->fwhm)
                printf("%g %g %g %g %g %g %g %g  %g  %d 0\n",
                       f->x, f->y, closest_record->x, closest_record->y,
                       closest_record->sigma_x *realtime->sigma_correction, closest_record->sigma_y*realtime->sigma_correction,
                       f->flux, closest_record->flux, closest_record->excess, closest_record->flags);
            /* else */
            /*     printf("%g %g 0 0 0 0 %g 0  0  0  0\n", */
            /*            f->x, f->y, f->flux); */

        }
}

void dump_objects(realtime_str *realtime, void *data)
{
    FILE *proc = popen("xpaset ds9 regions", "w");
    object_str *object;
    char *timestamp = time_str_get_date_time(realtime->time);
    int number = 0;
    int number_single = 0;
    int number_double = 0;
    int number_moving = 0;
    int number_meteor = 0;
    int number_flashes = 0;

    foreach(object, realtime->objects){
        record_str *record = NULL;
        record_str *record_prev = NULL;
        record_str *record0 = (record_str *)(list_last_item(object->records));

        if(object->state != OBJECT_SINGLE && object->state != OBJECT_DOUBLE){
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

        number ++;

        if(object->state == OBJECT_SINGLE)
            number_single ++;
        if(object->state == OBJECT_DOUBLE)
            number_double ++;
        if(object->state == OBJECT_MOVING)
            number_moving ++;
        if(object->state == OBJECT_METEOR)
            number_meteor ++;
        if(object->state == OBJECT_FLASH)
            number_flashes ++;
    }

    dprintf("%d objects (S: %d, D: %d, m: %d, M: %d, F:%d) at %s\n",
            number, number_single, number_double, number_moving, number_meteor, number_flashes, timestamp);

    free(timestamp);

    pclose(proc);
}

void dump_object_records(realtime_str *realtime, object_str *object, void *data)
{
    FILE *proc = popen("xpaset ds9 regions", "w");
    record_str *record = NULL;
    int need_sleep = TRUE;

    dprintf("Object had %d records\n", list_length(&object->records));

    foreach(record, object->records){
        fprintf(proc, "ellipse(%g, %g, %g, %g, %g)\n",
                record->x + 1, record->y + 1, record->a, record->b, record->theta*180./M_PI);
        need_sleep = TRUE;
    }

    pclose(proc);

    /* if(need_sleep) */
    /*     usleep(2e6); */
}

void alert_start(realtime_str *realtime, object_str *object, void *data)
{
    simulator_str *sim = (simulator_str *)data;
    record_str *record = (record_str *)list_first_item(object->records);

    dprintf("Alert for object %Ld at %g %g at length %d\n", object->id, record->x, record->y, object->length);

    if(object->length > 4){
        record_str *record0 = (record_str *)list_first_item(object->records);
        record_str *r = (record_str *)list_last_item(object->records);

        foreach(record, object->records){
            dprintf("%g %g %g %g %g\n",
                    1e-3*time_interval(record0->time, record->time),
                    record->x, record->y, record->sigma_x, record->sigma_y);
        }

        double closest_dist = 1e10;
        simulated_event_str *closest_ev = NULL;
        simulated_event_str *ev = NULL;

        foreach(ev, sim->events){
            double dist = sqrt((ev->x - r->x)*(ev->x - r->x) + (ev->y - r->y)*(ev->y - r->y));

            if(dist < 5 && dist < closest_dist){
                closest_dist = dist;
                closest_ev = ev;
            }
        }

        if(closest_ev){
            dprintf("Event type is %s, dist = %g, at %g %g\n",
                    (closest_ev->type == SIMULATED_FLASH ? "FLASH" : (closest_ev->type == SIMULATED_METEOR ? "METEOR" : "SATELLITE")),
                    closest_dist, closest_ev->x, closest_ev->y);

            //sleep(3);
        }
    }
}

void alert_cancel(realtime_str *realtime, object_str *object, void *data)
{
    simulator_str *sim = (simulator_str *)data;
    record_str *record0 = (record_str *)list_first_item(object->records);
    record_str *r = (record_str *)list_last_item(object->records);
    record_str *record = NULL;

    dprintf("Alert for object %Ld at %g %g cancelled at length %d\n", object->id, record0->x, record0->y, object->length);

    foreach(record, object->records){
        dprintf("%g %g %g %g %g\n",
                1e-3*time_interval(record0->time, record->time),
                record->x, record->y, record->sigma_x, record->sigma_y);
    }

    /* { */
    /*     prediction_str pr = realtime_predict_weighted(object, realtime->time, 1, realtime->initial_catch_radius); */

    /*     dprintf("Prediction for %g is at %g +/- %g, %g +/- %g\n", */
    /*             1e-3*time_interval(object->time0, realtime->time), */
    /*             pr.x, pr.dx, pr.y, pr.dy); */

    /*     pr = realtime_predict_weighted(object, object->time0, 1, realtime->initial_catch_radius); */
    /*     dprintf("Prediction for %g is at %g +/- %g, %g +/- %g\n", */
    /*             1e-3*time_interval(object->time0, object->time0), */
    /*             pr.x, pr.dx, pr.y, pr.dy); */
    /* } */

    double closest_dist = 1e10;
    simulated_event_str *closest_ev = NULL;
    simulated_event_str *ev = NULL;

    foreach(ev, sim->events){
        double dist = sqrt((ev->x - r->x)*(ev->x - r->x) + (ev->y - r->y)*(ev->y - r->y));

        if(dist < 5 && dist < closest_dist){
            closest_dist = dist;
            closest_ev = ev;
        }
    }

    if(closest_ev){
        dprintf("Event type is %s, dist = %g, at %g %g\n",
                (closest_ev->type == SIMULATED_FLASH ? "FLASH" : (closest_ev->type == SIMULATED_METEOR ? "METEOR" : "SATELLITE")),
                closest_dist, closest_ev->x, closest_ev->y);

        //sleep(3);
    }
}

void alert_finish(realtime_str *realtime, object_str *object, void *data)
{
    record_str *record = (record_str *)list_first_item(object->records);

    if(object->state != OBJECT_MOVING && object->state != OBJECT_METEOR)
        dprintf("Alert for object %Ld at %g %g finished at length %d\n", object->id, record->x, record->y, object->length);
}

int main(int argc, char **argv)
{
    char *filename_out = "/tmp/out.fits";
    char *filename_avg = "/tmp/out_avg.fits";
    char *filename_median = "/tmp/out_median.fits";
    char *filename_sigma = "/tmp/out_sigma.fits";
    char *filename_excess = "/tmp/out_excess.fits";
    simulator_str *sim = NULL;
    int width = 600;
    int height = 600;
    double ra0 = 56.75;
    double dec0 = 24.11;
    double pixscale = 25.9;

    parse_args(argc, argv,
               "width=%d", &width,
               "height=%d", &height,
               "ra0=%lf", &ra0,
               "dec0=%lf", &dec0,
               "pixscale=%lf", &pixscale,
               "%s", &filename_out,
               "%s", &filename_avg,
               "%s", &filename_sigma,
               NULL);

    sim = simulator_create(width, height);

    sim->time = time_current();
    sim->saturation = 65535;

    random_initialize(3);

    if(sim){
        realtime_str *rt = realtime_create(100);
        image_str *image = NULL;

        simulator_set_field(sim, ra0, dec0, pixscale);

        //REALTIME_SET_HOOK(rt, records_extracted_hook, dump_records, NULL);
        /* REALTIME_SET_HOOK(rt, records_extracted_hook, compare_flash_brightness, sim); */
        REALTIME_SET_HOOK(rt, objects_iterated_hook, dump_objects, NULL);
        REALTIME_SET_HOOK(rt, object_finished_hook, dump_object_records, NULL);
        /* REALTIME_SET_HOOK(rt, flash_detected_hook, alert_start, sim); */
        /* REALTIME_SET_HOOK(rt, flash_cancelled_hook, alert_cancel, sim); */

        sim->satellite_probability = 0.02;
        sim->meteor_probability = 0.01;
        sim->flash_probability = 0.02;

        /* sim->satellite_probability = 0.0; */
        /* sim->meteor_probability = 0.0; */
        /* sim->flash_probability = 0.0; */

        /* sim->amplify = 0; */

        /* sim->satellite_velocity_sigma = 15; */
        /* sim->satellite_mag = 3; */
        /* sim->satellite_mag_sigma = 0; */

        while(1){
        //while(sim->frame_number < 110){
            /* if(sim->frame_number == 110){ */
            /*     sim->satellite_probability = 0.03; */
            /*     sim->meteor_probability = 0.008; */
            /*     sim->flash_probability = 0.03; */
            /* } */

            image = simulator_get_image(sim);

            /* if(d == 101) */
            /*     simulator_make_satellite(sim); */
            /* if(d == 101) */
            /*     simulator_make_flash(sim); */

            if(image){
                image_str *aimage = NULL;//average_image_mean(rt->avg);
                image_str *mimage = average_image_median(rt->avg);
                image_str *simage = NULL;//average_image_sigma(rt->avg);
                image_str *eimage = average_image_excess_image(rt->avg, image);

                dprintf("Got image %Ld\n", sim->frame_number);

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
                ds9_set_filename(filename_excess);
                //usleep(1e6);

                realtime_process_frame(rt, image);

                /* if(sim->frame_number > 100){ */
                /*     char *command = make_string("xpaset -p ds9 saveimage png video/%05d.png", sim->frame_number); */

                /*     system(command); */

                /*     free(command); */
                /* } */

                //image_dump_to_fits(image, "/tmp/out_zero.fits");

                image_delete(image);
            }

            simulator_iterate(sim);
        }

        realtime_delete(rt);

        simulator_delete(sim);
    }

    return EXIT_SUCCESS;
}
