#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_multifit.h>

#include "utils.h"

#include "server.h"
#include "image_udp.h"
#include "average.h"
#include "command.h"
#include "sextractor.h"

#include "ports.h"

typedef struct {
    server_str *server;
    average_image_str *avg;

    connection_str *hw_connection;
    connection_str *grabber_connection;

    /* Parameters */
    int focus_step;
    int focus_limit1;
    int focus_limit2;
    int focus2_step;
    int focus2_limit1;
    int focus2_limit2;
    int filter;

    /* State variables */
    int is_acquiring;
    int is_quit;
    int is_dumping;

    int focus_internal;
    int focus2_internal;

    double focus_best;

    /* Current state */
    int state;
    /* Command last sent */
    int command;

    /* Focus measurements */
    int *measured_pos;
    double *measured_quality;
    int measured_length;

    int number;
    int focus;
} focuser_str;

#define STATE_NONE -1
#define STATE_INITIAL 0
#define STATE_FOCUS2_RESET 1
#define STATE_FOCUS2_RESET2 2
#define STATE_FOCUS2_MINIMAL 3
#define STATE_FOCUS_RESET 10
#define STATE_FOCUS_MINIMAL 11
#define STATE_FOCUS_INCREMENT 12
#define STATE_FOCUS2_INCREMENT 13
#define STATE_FOCUS_MEASURE 14
#define STATE_FOCUS_PROCESS 15
#define STATE_BEST_RESET 20
#define STATE_BEST_SET 21

#define STATE_FILTER 100

#define STATE_FINAL 999

#define COMMAND_NONE -1
#define COMMAND_REPLY_DEFAULT 0
#define COMMAND_REPLY_SHORT 1

#define SEND_COMMAND_CRI(focuser) do {server_connection_write_block((focuser)->hw_connection, "CN_CRI", 6); (focuser)->command = COMMAND_REPLY_DEFAULT; (focuser)->focus_internal = 0;} while(0)
#define SEND_COMMAND_CFQ(focuser, value) do {char str[7] = "CN_CFQ "; str[6] = value; server_connection_write_block((focuser)->hw_connection, str, 7); (focuser)->command = COMMAND_REPLY_DEFAULT; (focuser)->focus_internal = (value != 0) ? 4220 : 0;} while(0)
#define SEND_COMMAND_CFS(focuser, value) do {char str[8] = "CN_CFS  "; *(int16_t *)(str + 6) = value; server_connection_write_block((focuser)->hw_connection, str, 8); (focuser)->command = COMMAND_REPLY_DEFAULT; (focuser)->focus_internal += (value);} while (0)
#define SEND_COMMAND_GFS(focuser) do {server_connection_write_block((focuser)->hw_connection, "CN_GFS", 6); (focuser)->command = COMMAND_REPLY_SHORT;} while(0)
#define SEND_COMMAND_II_PWR(focuser, value) do {char str[7] = "II_PWR "; str[6] = (char)value; server_connection_write_block((focuser)->hw_connection, str, 7); (focuser)->command = COMMAND_REPLY_DEFAULT;} while(0)
#define SEND_COMMAND_FT_SET(focuser, value) do {char str[7] = "FT_SET "; str[6] = (char)value; server_connection_write_block((focuser)->hw_connection, str, 7); (focuser)->command = COMMAND_REPLY_DEFAULT;} while(0)
#define SEND_COMMAND_TL_CFQ(focuser) do {server_connection_write_block((focuser)->hw_connection, "TL_CFQ", 6); (focuser)->command = COMMAND_REPLY_DEFAULT; (focuser)->focus_internal = 0;} while(0)
#define SEND_COMMAND_TL_SET(focuser, value) do {char str[8] = "TL_SET  "; *(int16_t *)(str + 6) = value; server_connection_write_block((focuser)->hw_connection, str, 8); (focuser)->command = COMMAND_REPLY_DEFAULT; (focuser)->focus_internal += (value);} while (0)
#define SEND_COMMAND_TL_GET(focuser) do {server_connection_write_block((focuser)->hw_connection, "TL_GET", 6); (focuser)->command = COMMAND_REPLY_SHORT;} while(0)

focuser_str *focuser_create()
{
    focuser_str *focuser = (focuser_str *)malloc(sizeof(focuser_str));

    focuser->server = server_create();
    focuser->avg = average_image_create(10);

    focuser->hw_connection = NULL;

    focuser->focus_step = 10;
    focuser->focus_limit1 = 3550;
    focuser->focus_limit2 = 3700;
    focuser->focus2_step = 10;
    focuser->focus2_limit1 = 0;
    focuser->focus2_limit2 = 300;
    focuser->filter = 0;

    focuser->is_quit = FALSE;
    focuser->is_acquiring = FALSE;
    focuser->is_dumping = FALSE;

    focuser->focus_internal = 0;
    focuser->focus2_internal = 0;

    focuser->focus_best = 0;

    focuser->state = STATE_NONE;
    focuser->command = COMMAND_NONE;

    focuser->measured_pos = NULL;
    focuser->measured_quality = NULL;
    focuser->measured_length = 0;

    focuser->number = 0;

    return focuser;
}

void focuser_delete(focuser_str *focuser)
{
    average_image_delete(focuser->avg);
    server_delete(focuser->server);

    free(focuser);
}

void focuser_timer_event(server_str *, int , void *);
void focuser_processing_start(focuser_str *);
void focuser_processing_stop(focuser_str *);

void focuser_change_state(focuser_str *focuser, int state)
{
    dprintf("Entering state %d\n", state);

    focuser->state = state;

    switch(focuser->state){
    case(STATE_INITIAL):
        SEND_COMMAND_CRI(focuser);
        SEND_COMMAND_II_PWR(focuser, 1);
        focuser->command = COMMAND_REPLY_DEFAULT;
        server_add_timer(focuser->server, 1, STATE_FILTER, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS2_RESET):
        SEND_COMMAND_TL_SET(focuser, 100);
        server_add_timer(focuser->server, 5, STATE_FOCUS2_RESET2, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS2_RESET2):
        SEND_COMMAND_TL_CFQ(focuser);
        server_add_timer(focuser->server, 5, STATE_FOCUS2_MINIMAL, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS2_MINIMAL):
        SEND_COMMAND_TL_SET(focuser, focuser->focus2_limit2);
        server_add_timer(focuser->server, 5, STATE_FOCUS_RESET, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS_RESET):
        SEND_COMMAND_CFQ(focuser, 0);
        server_add_timer(focuser->server, 5, STATE_FOCUS_MINIMAL, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS_MINIMAL):
        SEND_COMMAND_CFS(focuser, focuser->focus_limit1);
        server_add_timer(focuser->server, 5, STATE_FOCUS_MEASURE, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS_MEASURE):
        if(focuser->focus_internal <= focuser->focus_limit2)
            focuser_change_state(focuser, STATE_FOCUS_PROCESS);
        else {
            //focuser_estimate_best_focus(focuser);
            if(focuser->focus2_internal <= focuser->focus2_limit2)
                focuser_change_state(focuser, STATE_FOCUS2_INCREMENT);
            else
                focuser_change_state(focuser, STATE_FINAL);
        }
        break;
    case(STATE_FOCUS_INCREMENT):
        SEND_COMMAND_CFS(focuser, focuser->focus_step);
        server_add_timer(focuser->server, 1, STATE_FOCUS_MEASURE, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS2_INCREMENT):
        SEND_COMMAND_TL_SET(focuser, focuser->focus2_step);
        server_add_timer(focuser->server, 1, STATE_FOCUS_RESET, focuser_timer_event, focuser);
        break;
    case(STATE_FOCUS_PROCESS):
        focuser_processing_start(focuser);
        break;
    case(STATE_BEST_RESET):
        SEND_COMMAND_CFQ(focuser, 0);
        server_add_timer(focuser->server, 5, STATE_BEST_SET, focuser_timer_event, focuser);
        break;
    case(STATE_BEST_SET):
        SEND_COMMAND_CFS(focuser, (int)focuser->focus_best);
        server_add_timer(focuser->server, 5, STATE_FINAL, focuser_timer_event, focuser);
        break;
    case(STATE_FILTER):
        SEND_COMMAND_FT_SET(focuser, focuser->filter);
        server_add_timer(focuser->server, 1, STATE_FOCUS_RESET, focuser_timer_event, focuser);
        break;
    case(STATE_FINAL):
        SEND_COMMAND_II_PWR(focuser, 0);
        focuser->is_quit = TRUE;
    default:
        break;
    }
}

void focuser_timer_event(server_str *server, int type, void *data)
{
    focuser_str *focuser = (focuser_str *)data;

    dprintf("Timer expired: %d\n", type);

    focuser_change_state(focuser, type);
}

void focuser_processing_start(focuser_str *focuser)
{
    focuser->is_acquiring = TRUE;
    focuser->number = 0;

    average_image_reset(focuser->avg);
}

void focuser_processing_stop(focuser_str *focuser)
{
    focuser->is_acquiring = FALSE;

    focuser_change_state(focuser, STATE_FOCUS_INCREMENT);
}

static void ds9_set_filename(char *filename)
{
    char *command = make_string("xpaset -p ds9 file fits %s", filename);

    system(command);

    free(command);
}

double focus_quality(image_str *image)
{
    struct list_head stars;
    double result = 0;

    sextractor_get_stars_list(image, 100000, &stars);

    image_dump_to_fits(image, "/tmp/out_ds9.fits");
    ds9_set_filename("/tmp/out_ds9.fits");

    result = list_length(&stars);

    free_list(stars);

    return result;
}

void dump_star_list(image_str *image, char *filename)
{
    FILE *file = fopen(filename, "w");
    struct list_head stars;
    record_str *star;

    sextractor_get_stars_list(image, 100000, &stars);

    foreach(star, stars){
        fprintf(file, "%g %g %g\n", star->x, star->y, star->flux);
    }

    free_list(stars);
    fclose(file);
}

void focuser_process_image(image_str *image, void *data)
{
    focuser_str *focuser = (focuser_str *)data;

    if(focuser->is_acquiring){
        if(focuser->is_dumping){
            char *filename = make_string("out_%04d_%04d_%04d.fits", focuser->focus_internal, focuser->focus2_internal, focuser->number);

            image_keyword_add_int(image, "IFOCUS", focuser->focus_internal, "Internal focus (shift from zero)");
            image_keyword_add_int(image, "IFOCUS2", focuser->focus_internal, "Internal focus2 (shift from zero)");

            dprintf("Saving image number %d with focus %d focus2 %d\n", focuser->number, focuser->focus_internal, focuser->focus2_internal);

            image_dump_to_fits(image, filename);
        }

        focuser->number ++;

        average_image_add(focuser->avg, image);

        if(average_image_is_full(focuser->avg)){
            image_str *aimage = average_image_median(focuser->avg);
            double quality = focus_quality(aimage);

            if(focuser->is_dumping){
                char *afilename =  make_string("out_%04d_%04d_median.fits", focuser->focus_internal, focuser->focus2_internal);
                char *sfilename =  make_string("out_%04d_%04d_median.stars", focuser->focus_internal, focuser->focus2_internal);

                image_dump_to_fits(aimage, afilename);
                dump_star_list(aimage, sfilename);

                free(afilename);
                free(sfilename);
            }

            /* Remember focus measurement */
            /* focuser->measured_pos = realloc(focuser->measured_pos, sizeof(int)*(focuser->measured_length + 1)); */
            /* focuser->measured_quality = realloc(focuser->measured_quality, sizeof(double)*(focuser->measured_length + 1)); */

            /* focuser->measured_pos[focuser->measured_length] = focuser->focus_internal; */
            /* focuser->measured_quality[focuser->measured_length] = quality; */

            /* focuser->measured_length ++; */

            dprintf("Measured focus quality: %g at %d %d\n", quality, focuser->focus_internal, focuser->focus2_internal);

            focuser_processing_stop(focuser);

            image_delete(aimage);
        }
    } else
        image_delete(image);
}

void focuser_process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    focuser_str *focuser = (focuser_str *)data;
    command_str *command = command_parse(string);

    if(command_match(command, "quit"))
        focuser->is_quit = TRUE;
}

void focuser_estimate_best_focus(focuser_str *focuser)
{
    FILE *file = fopen("out.focus", "w");
    gsl_multifit_linear_workspace *work = gsl_multifit_linear_alloc(focuser->measured_length, 3);
    gsl_matrix *X = gsl_matrix_alloc(focuser->measured_length, 3);
    gsl_vector *Y = gsl_vector_alloc(focuser->measured_length);
    gsl_vector *C = gsl_vector_alloc(3);
    gsl_matrix *cov = gsl_matrix_alloc(3, 3);
    double chisq = 0;
    int d;

    for(d = 0; d < focuser->measured_length; d++){
        gsl_matrix_set(X, d, 0, 1.0);
        gsl_matrix_set(X, d, 1, focuser->measured_pos[d]);
        gsl_matrix_set(X, d, 2, focuser->measured_pos[d]*focuser->measured_pos[d]);

        gsl_vector_set(Y, d, focuser->measured_quality[d]);
    }

    gsl_multifit_linear(X, Y, C, cov, &chisq, work);

    focuser->focus_best = -0.5*gsl_vector_get(C, 1)/gsl_vector_get(C, 2);

    dprintf("Best focus is at %g\n", focuser->focus_best);

    /* Sanity check */
    if(focuser->focus_best < focuser->focus_limit1 || focuser->focus_best > focuser->focus_limit2)
        focuser->focus_best = 0.5*(focuser->focus_limit1 + focuser->focus_limit2);

    gsl_matrix_free(X);
    gsl_vector_free(Y);
    gsl_vector_free(C);
    gsl_matrix_free(cov);

    gsl_multifit_linear_free (work);

    fclose(file);
}

int focuser_process_hw_reply(server_str *server, connection_str *connection, char *data, int length, void *ptr)
{
    focuser_str *focuser = (focuser_str *)ptr;
    char *string = make_long_hex_string(data, length);
    int processed = 0;
    int code = -1;
    int error = -1;
    int value = -1;

    dprintf("\nHW reply: %d bytes (waiting for reply on command %d), state = %d\n", length, focuser->command, focuser->state);
    dprintf("%s", string);

    switch(focuser->command){
    case(COMMAND_REPLY_DEFAULT):
        processed = 1;
        error = *((u_int8_t *)data);
        dprintf("error = %d\n", error);
        break;
    case(COMMAND_REPLY_SHORT):
        processed = 4;
        code = *((u_int8_t *)data);
        error = *((u_int8_t *)(data + 1));
        value = *((int16_t *)(data + 2));
        dprintf("code = %d, error = %d, value = %d\n", code, error, value);
        break;
    default:
        break;
    }

    focuser->command = COMMAND_REPLY_DEFAULT;

    /* if(processed){ */
    /*     switch(focuser->state){ */
    /*     case(STATE_FOCUS_MEASURE): */
    /*         focuser->focus_current = value; */
    /*         if(focuser->focus_internal <= focuser->focus_limit2) */
    /*             focuser_change_state(focuser, STATE_FOCUS_PROCESS); */
    /*         else { */
    /*             focuser_estimate_best_focus(focuser); */
    /*             focuser_change_state(focuser, STATE_BEST_RESET); */
    /*         } */
    /*         break; */
    /*     default: */
    /*         break; */
    /*     } */
    /* } */

    free(string);

    dprintf("processed %d bytes\n\n", processed);

    return processed;
}

int main(int argc, char **argv)
{
    focuser_str *focuser = focuser_create();
    image_udp_str *udp = NULL;
    char *host = "localhost";
    int port = 5559;
    int udp_port = PORT_GRABBER_UDP;
    char *hw_host = "localhost";
    int hw_port = PORT_CHANNEL_HW;
    char *grabber_host = "localhost";
    int grabber_port = PORT_GRABBER_CONTROL;

    parse_args(argc, argv,
               "port=%d", &port,
               "udp_port=%d", &udp_port,
               "hw_host=%s", &hw_host,
               "hw_port=%d", &hw_port,
               "grabber_host=%s", &grabber_host,
               "grabber_port=%d", &grabber_port,
               "step=%d", &focuser->focus_step,
               "limit1=%d", &focuser->focus_limit1,
               "limit2=%d", &focuser->focus_limit2,
               "focus2_step=%d", &focuser->focus2_step,
               "focus2_limit1=%d", &focuser->focus2_limit1,
               "focus2_limit2=%d", &focuser->focus2_limit2,
               "filter=%d", &focuser->filter,
               "-dump", &focuser->is_dumping,
               NULL);

    server_listen(focuser->server, host, port);

    focuser->hw_connection = server_add_connection(focuser->server, hw_host, hw_port);
    focuser->hw_connection->data_read_hook = focuser_process_hw_reply;
    focuser->hw_connection->data_read_hook_data = focuser;
    focuser->hw_connection->is_active = TRUE;

    focuser->grabber_connection = server_add_connection(focuser->server, grabber_host, grabber_port);
    server_connection_message(focuser->grabber_connection, "CONTROL NETSTART");

    SERVER_SET_HOOK(focuser->server, line_read_hook, focuser_process_command, focuser);

    udp = image_udp_attach(focuser->server, udp_port);

    udp->callback_image = focuser_process_image;
    udp->callback_image_data = focuser;

    focuser_change_state(focuser, 0);

    while(!focuser->is_quit){
        server_cycle(focuser->server, 100);
    }

    image_udp_detach(focuser->server, udp);

    focuser_delete(focuser);

    return EXIT_SUCCESS;
}
