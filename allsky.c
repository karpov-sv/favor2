#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>

#include <gsl/gsl_multifit.h>

#include "utils.h"

#include "server.h"
#include "image_udp.h"
#include "command.h"
#include "db.h"
#include "ports.h"

char* image_dir = NULL;
int frame_index = 0;
int is_ds9 = FALSE;
int is_jpeg = FALSE;

typedef struct {
    server_str *server;
    connection_str *grabber_connection;
    connection_str *sqm_connection;
    db_str *db;

    double interval;

    double exposure;
    double sqm_brightness;
    double sqm_temperature;

    image_str *image;
    int length;
    int coadd;

    char *base;

    time_str last_stored_time;
    time_str last_current_time;

    time_str last_stored_sqm_time;
    time_str last_stored_boltwood_time;

    char *boltwood_filename;
    /* Boltwood Cloud Sensor II data */
    double sky_ambient_temperature;
    double ambient_temperature;
    double sensor_temperature;
    double wind;
    double humidity;
    double dewpoint;

    /* See Boltwood manual for exact meanings */
    int rain_flag;
    int wet_flag;
    int cloud_cond;
    int wind_cond;
    int rain_cond;
    int day_cond;

    time_str last_grabber_time;
    time_str last_sqm_time;

    int is_quit;
} allsky_str;

static void ds9_set_filename(char *filename)
{
    system_run("xpaset -p ds9 file fits %s", filename);
}

image_str *image_clean_allsky(image_str *image0, char *darkname)
{
    image_str *dark = image_create_from_fits(darkname);
    image_str *image = image_create_double(image0->width, image0->height);
    image_str *image2 = image_create_double(image0->width, image0->height);
    double a = 1;
    double b = 0;
    int x;
    int y;

    if(dark){
        int size0 = 80;

        /* Compute fit for dark frame */
        {
            int N = size0*size0;
            gsl_multifit_robust_workspace *work = gsl_multifit_robust_alloc(gsl_multifit_robust_bisquare, size0*size0, 2);
            gsl_matrix *X = gsl_matrix_alloc(N, 2);
            gsl_vector *Y = gsl_vector_alloc(N);
            gsl_vector *C = gsl_vector_alloc(2);
            gsl_matrix *cov = gsl_matrix_alloc(2, 2);
            int d = 0;
            int status = 0;

            for(y = 0; y < size0; y++)
                for(x = 0; x < size0; x++){
                    gsl_matrix_set(X, d, 0, 1.0);
                    gsl_matrix_set(X, d, 1, dark->double_data[y*image0->width + x]);

                    gsl_vector_set(Y, d, image0->data[y*image0->width + x]);

                    /* printf("%g %g\n", dark->double_data[y*image0->width + x], 1.0*image0->data[y*image0->width + x]); */

                    d++;
                }

            status = gsl_multifit_robust(X, Y, C, cov, work);

            if(!status){
                a = gsl_vector_get(C, 1);
                b = gsl_vector_get(C, 0);
            }

            dprintf("a=%g b=%g\n", a, b);

            gsl_matrix_free(X);
            gsl_vector_free(Y);
            gsl_vector_free(C);
            gsl_matrix_free(cov);

            gsl_multifit_robust_free (work);
        }
    }

    /* Subtract the normalized dark frame */
    for(y = 0; y < image->height; y++)
        for(x = 0; x < image->width; x++){
            double value0 = image0->data[y*image->width + x];
            double dvalue = dark ? dark->double_data[y*image->width + x] : 0;
            double value = value0 - a*dvalue - b;

            /* Clean */
            if(dark && (value0 >= 4090 || dvalue >= 4090)){
                int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                double sum = 0;
                int N = 0;
                int d;

                for(d = 0; d < 8; d++){
                    if(x+dx[d] > 0 && x+dx[d] < image->width - 1 &&
                       y+dy[d] > 0 && y+dy[d] < image->height - 1){
                        sum += image0->data[(y+dy[d])*image->width + x+dx[d]] - a*dark->double_data[(y+dy[d])*image->width + x+dx[d]] - b;
                        N++;
                    }
                }

                value = sum*1./N;
            }

            image->double_data[y*image->width + x] = value;
        }

    /* Median filter */
    for(y = 0; y < image->height; y++)
        for(x = 0; x < image->width; x++){
            int dx[] = {-1, 0, 1, -1, 0, 1, -1, 0, 1};
            int dy[] = {-1, -1, -1, 0, 0, 0, 1, 1, 1};
            double val[9];
            int d;
            int N = 0;

            for(d = 0; d < 9; d++){
                if(x+dx[d] > 0 && x+dx[d] < image->width - 1 &&
                   y+dy[d] > 0 && y+dy[d] < image->height - 1){
                    val[d] = image->double_data[(y+dy[d])*image->width + x+dx[d]];
                    N++;
                }
            }

            if(N == 9){
                int cmpfn(const void *a, const void *b)
                {
                    return (*(double*)a - *(double*)b);
                }

                qsort(val, 9, sizeof(double), cmpfn);

                image2->double_data[y*image->width + x] = val[4];
            } else
                image2->double_data[y*image->width + x] = image->double_data[y*image->width + x];;
        }

    if(dark)
        image_delete(dark);
    image_delete(image);

    return image2;
}

static void allsky_set_exposure(allsky_str *allsky, double exposure)
{
    dprintf("Setting exposure to %g (now %g)\n", exposure, allsky->exposure);
    server_connection_message(allsky->grabber_connection, "SET EXPOSURE %lf\n", exposure);
}

static void allsky_store_image(allsky_str *allsky, image_str *image, image_str *clean)
{
    time_str evening_time = time_incremented(image->time, -12*60*60);
    char *night = make_string("%04d_%02d_%02d", evening_time.year, evening_time.month, evening_time.day);
    char *dirname = make_string("%s/%s", allsky->base, night);
    char *filename = make_string("%s/%04d%02d%02d_%02d%02d%02d.jpg", dirname,
                                 image->time.year, image->time.month, image->time.day,
                                 image->time.hour, image->time.minute, image->time.second);
    char *fitsname = make_string("%s/%04d%02d%02d_%02d%02d%02d.fits", dirname,
                                 image->time.year, image->time.month, image->time.day,
                                 image->time.hour, image->time.minute, image->time.second);
    char *timestamp = time_str_get_date_time(image->time);
    double exposure = image_keyword_get_double(image, "EXPOSURE");
    char *sql = make_string("INSERT INTO meteo_images (filename, night, time, exposure) "
                            "VALUES ('%s', '%s', '%s', %g);",
                            filename, night, timestamp, exposure);

    mkdir(allsky->base, 0755);
    mkdir(dirname, 0755);

    image_jpeg_set_scale(1);
    if(clean)
        image_dump_to_jpeg(clean, filename);
    else
        image_dump_to_jpeg(image, filename);
    image_dump_to_fits(image, fitsname);

    db_query(allsky->db, sql);

    free(sql);
    free(timestamp);
    free(filename);
    free(fitsname);
    free(dirname);
    free(night);
}

static void allsky_store_current(allsky_str *allsky, image_str *image, image_str *clean)
{
    char *currentname = make_string("%s/current.jpg", allsky->base);
    char *newname = make_string("%s/current.new.jpg", allsky->base);

    mkdir(allsky->base, 0755);

    image_jpeg_set_scale(1);

    if(clean)
        image_dump_to_jpeg(clean, newname);
    else
        image_dump_to_jpeg(image, newname);

    rename(newname, currentname);

    image_dump_to_fits(image, "current.fits");

    free(currentname);
    free(newname);
}

static void allsky_process_image(image_str *image, void *data)
{
    allsky_str *allsky = (allsky_str *)data;
    double median = image_median(image);
    double exposure = image_keyword_get_double(image, "EXPOSURE");
    image_str *clean = NULL;

    dprintf("Got image\n");

    allsky->exposure = exposure;

    if(allsky->coadd > 1){
        if(!allsky->length)
            allsky->image = image_create_double(image->width, image->height);

        image_add(allsky->image, image);
        image_delete(image);
        image = NULL;
        allsky->length ++;

        if(allsky->length >= allsky->coadd){
            image = allsky->image;
            allsky->image = NULL;
            allsky->length = 0;
        }
    }

    if(!image)
        return;

    /* Clean image if necessary */
    if(exposure > 1)
        clean = image_clean_allsky(image, "allsky.low.fits");

    /* Adjust exposure if necessary */
    if(median > 2000 && exposure > 5e-6){
        allsky_set_exposure(allsky, MAX(5e-6, exposure*0.5));
        allsky->coadd = 1;
    } else if(median < 1000 && exposure < 10.0) {
        allsky_set_exposure(allsky, MIN(10.0, exposure/0.5));
        allsky->coadd = 1;
    /* } else if(median < 1000 && allsky->coadd < 10){ */
    /*     allsky->coadd = 10; */
    } else {
        /* Normal exposure, store image if necessary */

        if(1e-3*time_interval(allsky->last_stored_time, image->time) > allsky->interval){
            allsky_store_image(allsky, image, clean);

            allsky->last_stored_time = image->time;
        }
    }

    if(1e-3*time_interval(allsky->last_current_time, image->time) > 1){
        allsky_store_current(allsky, image, clean);

        allsky->last_current_time = image->time;

        if(is_ds9){
            image_dump_to_fits(image, "/tmp/allsky_ds9_image.fits");
            ds9_set_filename("/tmp/allsky_ds9_image.fits");
        }
    }

    image_delete(image);
    image_delete(clean);
}

static void allsky_store_sqm(allsky_str *allsky, double brightness, double temperature)
{
    char *timestamp = time_str_get_date_time(time_current());
    char *sql = make_string("INSERT INTO meteo_sqm (time, temp, brightness) "
                            "VALUES ('%s', %g, %g);",
                            timestamp, temperature, brightness);

    db_query(allsky->db, sql);

    free(sql);
    free(timestamp);
}

static void allsky_store_current_params(allsky_str *allsky)
{
    char *currentname = make_string("%s/current.params", allsky->base);
    char *newname = make_string("%s/current.new.params", allsky->base);
    FILE *file = fopen(newname, "w");

    mkdir(allsky->base, 0755);

    fprintf(file, "%g %g %g %g %g %g %g %g %g %d %d %d %d %d %d\n",
            allsky->exposure, allsky->sqm_brightness, allsky->sqm_temperature,
            allsky->sensor_temperature, allsky->sky_ambient_temperature,
            allsky->ambient_temperature, allsky->wind, allsky->humidity, allsky->dewpoint,
            allsky->rain_flag, allsky->wet_flag, allsky->cloud_cond,
            allsky->wind_cond, allsky->rain_cond, allsky->day_cond);

    fclose(file);

    rename(newname, currentname);

    free(currentname);
    free(newname);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    allsky_str *allsky = (allsky_str *)data;

    if(connection == allsky->grabber_connection){
        server_connection_message(connection, "CONTROL NETSTART");
        allsky_set_exposure(allsky, allsky->exposure);
    } else if(connection == allsky->sqm_connection)
        server_connection_write_block(connection, "rx", 2);

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    /* allsky_str *allsky = (allsky_str *)data; */

    server_default_connection_disconnected_hook(server, connection, NULL);
}

void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    allsky_str *allsky = (allsky_str *)data;
    command_str *command = command_parse(string);

    /* server_default_line_read_hook(server, connection, string, data); */

    if(connection == allsky->grabber_connection){
        //dprintf("Reply from grabber received\n");
        allsky->last_grabber_time = time_current();
    }

    if(command_match(command, "exit") || command_match(command, "quit")){
        allsky->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_weather_status")){
        server_connection_message(connection, "weather_status "
                                  "sensor_temp=%g sky_ambient_temp=%g ambient_temp=%g wind=%g humidity=%g dewpoint=%g "
                                  "rain_flag=%d wet_flag=%d cloud_cond=%d wind_cond=%d rain_cond=%d day_cond=%d "
                                  "brightness=%g",
                                  allsky->sensor_temperature, allsky->sky_ambient_temperature,
                                  allsky->ambient_temperature, allsky->wind, allsky->humidity, allsky->dewpoint,
                                  allsky->rain_flag, allsky->wet_flag, allsky->cloud_cond,
                                  allsky->wind_cond, allsky->rain_cond, allsky->day_cond,
                                  allsky->sqm_brightness);
    } else if(connection == allsky->sqm_connection){
        double brightness = 0;
        double temperature = 0;
        double tmp = 0;
        int itmp = 0;

        /* dprintf("> %s\n", string); */

        if(sscanf(string, "r,%lfm,%dHz,%dc,%lfs,%lfC", &brightness, &itmp, &itmp, &tmp, &temperature)){
            /* dprintf("brightness = %g, temperature = %g\n", brightness, temperature); */
            allsky->sqm_brightness = brightness;
            allsky->sqm_temperature = temperature;

            if(1e-3*time_interval(allsky->last_stored_sqm_time, time_current()) > allsky->interval){
                allsky_store_sqm(allsky, brightness, temperature);
                allsky->last_stored_sqm_time = time_current();
            }

            allsky_store_current_params(allsky);
        } else
            dprintf("wrong reply: %s\n", string);

        allsky->last_sqm_time = time_current();
    }

    command_delete(command);
}

void allsky_boltwood_update(allsky_str *allsky)
{
    FILE *file = allsky->boltwood_filename ? fopen(allsky->boltwood_filename, "r") : NULL;

    if(file){
        char buffer[1024];
        int len = fread(buffer, 1, 1024, file);

        if(len)
            buffer[len] = '\0';

        /* Rough way to replace all commas with dots */
        while(len > 0){
            len--;
            if(buffer[len] == ',')
                buffer[len] = '.';
        }

        if(buffer[0]){
            char timestr[23];
            char t_f;
            char v_f;
            double heater;
            double sky_amb_temp = 0;
            double wind = 0;

            time_str time __attribute__ ((unused));

            sscanf(buffer, "%22c %c %c %lf %lf %lf %lf %lf %lf %lf %d %d %*f %*f %d %d %d %d",
                   timestr, &t_f, &v_f, &sky_amb_temp, &allsky->ambient_temperature,
                   &allsky->sensor_temperature, &wind, &allsky->humidity,
                   &allsky->dewpoint, &heater, &allsky->rain_flag, &allsky->wet_flag,
                   &allsky->cloud_cond, &allsky->wind_cond, &allsky->rain_cond, &allsky->day_cond);

            if(wind >= 0){
                /* Normal conditions, all data valid */
                allsky->sky_ambient_temperature = sky_amb_temp;
                allsky->wind = wind;
            } else {
                /* Keep previous values */
            }

            time = time_str_from_date_time(timestr);

            /* TODO: check units reported in t_f and v_f, and correct values if necessary */
            if(v_f == 'K')
                allsky->wind *= 0.277778; /* kmph to m/s */
            else if(v_f == 'M')
                allsky->wind *= 0.44704; /* mph to m/s */

            if(1e-3*time_interval(allsky->last_stored_boltwood_time, time_current()) > allsky->interval){
                /* The sensor is properly initialized */
                char *timestamp = time_str_get_date_time(time_current());
                char *sql = make_string("INSERT INTO meteo_parameters ("
                                        "time, sensor_temp, sky_ambient_temp, ambient_temp, wind, humidity, dewpoint, "
                                        "rain_flag, wet_flag, cloud_cond, wind_cond, rain_cond, day_cond"
                                        ") VALUES ('%s', %g, %g, %g, %g, %g, %g, %d, %d, %d, %d, %d, %d);",
                                        timestamp, allsky->sensor_temperature, allsky->sky_ambient_temperature,
                                        allsky->ambient_temperature, allsky->wind, allsky->humidity, allsky->dewpoint,
                                        allsky->rain_flag, allsky->wet_flag, allsky->cloud_cond,
                                        allsky->wind_cond, allsky->rain_cond, allsky->day_cond);

                db_query(allsky->db, sql);

                allsky->last_stored_boltwood_time = time_current();

                free(sql);
                free(timestamp);
            }
        }

        fclose(file);
    }
}

void allsky_request_state_cb(server_str *server, int type, void *data)
{
    allsky_str *allsky = (allsky_str *)data;

    if(allsky->sqm_connection->is_connected){
        if(time_interval(allsky->last_sqm_time, time_current()) > 1e3*100)
            server_connection_restart(allsky->server, allsky->sqm_connection);
        else
            server_connection_write_block(allsky->sqm_connection, "rx", 2);
    }

    allsky_boltwood_update(allsky);

    if(allsky->grabber_connection && allsky->grabber_connection->is_connected){
        if(time_interval(allsky->last_grabber_time, time_current()) > 1e3*100)
            server_connection_restart(allsky->server, allsky->grabber_connection);
        else
            server_connection_message(allsky->grabber_connection, "CONTROL NETSTART");
    }

    server_add_timer(server, 1.0, 0, allsky_request_state_cb, allsky);
}

int main(int argc, char **argv)
{
    allsky_str *allsky = NULL;
    image_udp_str *udp = NULL;
    char *base = "METEO";
    char *grabber_host = "localhost";
    int grabber_port = PORT_GRABBER_CONTROL;
    int udp_port = PORT_GRABBER_UDP;
    char *sqm_host = "192.168.1.11";
    int sqm_port = 10001;
    char *filename = NULL;
    int port = PORT_WEATHER;

    double exposure = 0.1;
    double interval = 120.0;

    parse_args(argc, argv,
               "port=%d", &port,
               "grabber_host=%s", &grabber_host,
               "grabber_port=%d", &grabber_port,
               "udp_port=%d", &udp_port,
               "sqm_host=%s", &sqm_host,
               "sqm_port=%d", &sqm_port,
               "base=%s", &base,
               "exposure=%lf", &exposure,
               "interval=%lf", &interval,
               "boltwood_file=%s", &filename,
               "-ds9", &is_ds9,
               "-jpeg", &is_jpeg,
               NULL);

    allsky = malloc(sizeof(allsky_str));
    allsky->server = server_create();
    allsky->db = db_create("dbname=favor2");

    allsky->base = base;
    allsky->interval = interval;

    allsky->exposure = exposure;
    allsky->sqm_brightness = 0;
    allsky->sqm_temperature = 0;

    allsky->sky_ambient_temperature = 0;
    allsky->ambient_temperature = 0;
    allsky->sensor_temperature = 0;
    allsky->wind = 0;
    allsky->humidity = 0;
    allsky->dewpoint = 0;

    allsky->rain_flag = 0;
    allsky->wet_flag = 0;
    allsky->cloud_cond = 0;
    allsky->wind_cond = 0;
    allsky->rain_cond = 0;
    allsky->day_cond = 0;

    allsky->image = NULL;
    allsky->coadd = 1;
    allsky->length = 0;

    allsky->grabber_connection = server_add_connection(allsky->server, grabber_host, grabber_port);
    allsky->grabber_connection->is_active = TRUE;
    allsky->last_grabber_time = time_current();
    allsky->last_sqm_time = time_current();

    allsky->sqm_connection = server_add_connection(allsky->server, sqm_host, sqm_port);
    allsky->sqm_connection->is_active = TRUE;

    /* Set up custom network command processing code */
    SERVER_SET_HOOK(allsky->server, line_read_hook, process_command, allsky);
    /* Set up custom network post-connection code */
    SERVER_SET_HOOK(allsky->server, connection_connected_hook, connection_connected, allsky);
    /* Set up custom network connection lost hook */
    SERVER_SET_HOOK(allsky->server, connection_disconnected_hook, connection_disconnected, allsky);

    udp = image_udp_attach(allsky->server, udp_port);
    udp->callback_image = allsky_process_image;
    udp->callback_image_data = allsky;

    allsky->last_stored_time = time_zero();
    allsky->last_current_time = time_zero();
    allsky->last_stored_sqm_time = time_zero();
    allsky->last_stored_boltwood_time = time_zero();

    server_add_timer(allsky->server, 1.0, 0, allsky_request_state_cb, allsky);

    allsky->boltwood_filename = filename;

    server_listen(allsky->server, "localhost", port);

    image_jpeg_set_percentile(0.01, 0.9995);

    allsky->is_quit = FALSE;

    while(!allsky->is_quit){
        server_cycle(allsky->server, 1);
    }

    db_delete(allsky->db);
    image_udp_detach(allsky->server, udp);
    server_delete(allsky->server);

    return EXIT_SUCCESS;
}
