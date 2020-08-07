#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils.h"

#include "ports.h"
#include "server.h"
#include "command.h"
#include "command_queue.h"
#include "sequencer.h"
#include "scheduler.h"

/* Hierarchy of states */

typedef struct {
    server_str *server;

    connection_str *rem_connection;
    connection_str *channel_connection;
    connection_str *logger_connection;

    scheduler_str *sched;

    int is_night;
    int is_open;
    int is_observing;
    int is_light;
    int is_below;

    double ra;
    double dec;
    double next_ra;
    double next_dec;
    double is_safe;

    int is_quit;
} beholder_str;

static int is_night(beholder_str *beholder)
{
    double zsun = scheduler_sun_zenith_angle(beholder->sched, time_current());

    return (zsun > 90 + 5);
}

/* Returns the derivative of position angle in deg/s */
static double get_dotP(beholder_str *beholder, double ra, double dec)
{
    time_str time = time_current();
    double S = time_str_get_local_sidereal_time(time, beholder->sched->longitude);
    double t = 15*(S - ra);
    double phi = beholder->sched->latitude;
    double Z = acos(cos(phi*M_PI/180)*cos(dec*M_PI/180)*cos(t*M_PI/180) + sin(phi*M_PI/180)*sin(dec*M_PI/180))*180/M_PI;
    double sinP = sin(t*M_PI/180)*cos(phi*M_PI/180)/sin(Z*M_PI/180);
    double cosP = sqrt(1 - sinP*sinP); /* FIXME: modulus */
    double dotZ = cos(phi*M_PI/180)*cos(dec*M_PI/180)*sin(t*M_PI/180)/sin(Z*M_PI/180)*360/86400;
    double dotP = cos(t*M_PI/180)*cos(phi*M_PI/180)/sin(Z*M_PI/180)/cosP*360/86400 - sin(t*M_PI/180)*cos(phi*M_PI/180)*cos(Z*M_PI/180)/pow(sin(Z*M_PI/180), 2)*dotZ;

    dotP = cos(phi*M_PI/180)/sin(Z*M_PI/180)/cosP*(cos(t*M_PI/180) - pow(sin(t*M_PI/180), 2)*cos(phi*M_PI/180)*cos(Z*M_PI/180)*cos(dec*M_PI/180)/pow(sin(Z*M_PI/180), 2))*60./86400;

    return fabs(dotP);
}

static int is_safe(beholder_str *beholder, double ra, double dec)
{
    int result = TRUE;

    double dsun = 0;
    double dmoon = 0;
    double dzenith = 0;
    double shift = get_dotP(beholder, ra, dec)*20*60/180*M_PI;
    int night = is_night(beholder);

    scheduler_distances_from_point(beholder->sched, time_current(), ra, dec, &dsun, &dmoon, &dzenith);

    /* We won't observe near the Moon */
    if(dmoon < 20)
        result = FALSE;
    /* ..or near the horizon */
    if((beholder->is_below && dzenith > 60) ||
       (!beholder->is_below && dzenith > 75))
        result = FALSE;
    /* ..and of course not at the day time! */
    if(!night)
        result = FALSE;

    dprintf("Safety check: moon %g zenith %g night %d shift %g safety %d\n", dmoon, dzenith, night, shift, result);

    return result;
}

void beholder_log(beholder_str *beholder, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    if(beholder->logger_connection)// && beholder->logger_connection->is_connected)
        server_connection_message(beholder->logger_connection, buffer);

    free(buffer);
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    command_str *command = command_parse(string);

    server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        beholder->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "iam")){
        char *class = NULL;

        command_args(command, "class=%s", &class, NULL);

        if(!strcmp(class, "channel")){
            dprintf("Channel connected\n");
            beholder->channel_connection = connection;
        } else
            dprintf("Client with unknown class %s connected\n", class);
    } else if(command_match(command, "night_begin")){
        beholder_log(beholder, "Night started");
        beholder->is_night = TRUE;
        server_connection_message(beholder->channel_connection, "grabber_start");
        server_connection_message(beholder->channel_connection, "write_darks");
    } else if(command_match(command, "dome_open")){
        beholder_log(beholder, "Dome opened");
        beholder->is_open = TRUE;
        server_connection_message(beholder->channel_connection, "grabber_start");
    } else if(command_match(command, "next_target")){
        char *name = NULL;

        command_args(command,
                     "ra=%lf", &beholder->next_ra,
                     "dec=%lf", &beholder->next_dec,
                     "name=%s", &name,
                     NULL);

        server_connection_message(beholder->channel_connection,
                                  "will_repoint_to ra=%g dec=%g name=%s",
                                  beholder->next_ra, beholder->next_dec,
                                  name ? name : "unknown");

        /* Update of current coordinates are slow, so let's use target ones for now */
        beholder->ra = beholder->next_ra;
        beholder->dec = beholder->next_dec;

        beholder_log(beholder, "Next pointing is to %g %g, %s", beholder->next_ra, beholder->next_dec, name);
    } else if(command_match(command, "current_pointing")){
        char *name = NULL;

        command_args(command,
                     "ra=%lf", &beholder->ra,
                     "dec=%lf", &beholder->dec,
                     "target_ra=%lf", &beholder->next_ra,
                     "target_dec=%lf", &beholder->next_dec,
                     "target_name=%s", &name,
                     "below=%d", &beholder->is_below,
                     "open=%d", &beholder->is_open,
                     NULL);

        beholder->is_safe = is_safe(beholder, beholder->next_ra, beholder->next_dec);

        /* If, suddenly, the safety fails while we observe, stop the observations */
        if(beholder->is_observing && !beholder->is_safe){
            beholder_log(beholder, "Pointing now unsafe, finishing observations");
            beholder->is_observing = FALSE;
            server_connection_message(beholder->channel_connection, "processing_stop");
            server_connection_message(beholder->channel_connection, "storage_stop");
            server_connection_message(beholder->channel_connection, "ii_stop");
        }

        /* Pass the message forward to the channel */
        server_connection_message(beholder->channel_connection, string);
    } else if(command_match(command, "tracking_start")){
        /* Tracking commands are only processed at night with open dome */
        if(beholder->is_night && beholder->is_open){
            if(beholder->is_safe){
                beholder_log(beholder, "REM tracking started, observing");
                beholder->is_observing = TRUE;
                server_connection_message(beholder->channel_connection, "ii_start");
                server_connection_message(beholder->channel_connection, "grabber_start");
                server_connection_message(beholder->channel_connection, "processing_start");
                server_connection_message(beholder->channel_connection, "storage_start");
            } else {
                beholder_log(beholder, "REM tracking started, pointing unsafe, will write darks");
                beholder->is_observing = FALSE;
                /* Shut down everything just in case, should not be necessary */
                server_connection_message(beholder->channel_connection, "storage_stop");
                server_connection_message(beholder->channel_connection, "ii_stop");
                server_connection_message(beholder->channel_connection, "write_darks");
            }
        } else
            beholder_log(beholder, "REM tracking started, not observing");
    } else if(command_match(command, "tracking_stop")){
        if(beholder->is_observing){
            beholder_log(beholder, "REM tracking stopped, finishing observations");
            beholder->is_observing = FALSE;
            server_connection_message(beholder->channel_connection, "processing_stop");
            server_connection_message(beholder->channel_connection, "storage_stop");
            server_connection_message(beholder->channel_connection, "ii_stop");
        } else
            beholder_log(beholder, "REM tracking stopped");
    } else if(command_match(command, "dome_close")){
        beholder_log(beholder, "Dome closed");
        beholder->is_open = FALSE;
        beholder->is_observing = FALSE;
        /* Shut down everything just in case, should not be necessary */
        server_connection_message(beholder->channel_connection, "processing_stop");
        server_connection_message(beholder->channel_connection, "storage_stop");
        server_connection_message(beholder->channel_connection, "ii_stop");
    } else if(command_match(command, "night_end")){
        beholder_log(beholder, "Night ended");
        beholder->is_night = FALSE;
        beholder->is_observing = FALSE;
        /* Shut down everything just in case, should not be necessary */
        server_connection_message(beholder->channel_connection, "processing_stop");
        server_connection_message(beholder->channel_connection, "storage_stop");
        server_connection_message(beholder->channel_connection, "ii_stop");
        /* Final darks */
        server_connection_message(beholder->channel_connection, "write_darks");
        /* Perform some post-processing */
        /* server_connection_message(beholder->channel_connection, "postprocess"); */
    } else if(command_match(command, "unknown_command")){
    } else {
        dprintf("unknown_command %s\n", command_name(command));
        //server_connection_message(connection, "unknown_command %s", command_name(command));
    }

    command_delete(command);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    beholder_str *beholder = (beholder_str *)data;

    if(connection == beholder->logger_connection){
        server_connection_message(connection, "iam id=beholder");
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    beholder_str *beholder = (beholder_str *)data;

    if(connection == beholder->channel_connection){
        dprintf("Channel disconnected\n");
        beholder->channel_connection = NULL;
    }

    server_default_connection_disconnected_hook(server, connection, NULL);
}

int main(int argc, char **argv)
{
    beholder_str *beholder = (beholder_str *)malloc(sizeof(beholder_str));
    int port = PORT_BEHOLDER;
    char *rem_host = "localhost";
    int rem_port = PORT_REM;
    char *channel_host = "localhost";
    int channel_port = PORT_CHANNEL_CONTROL;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;

    beholder->sched = scheduler_create(15);
    beholder->sched->latitude = -29.25666667;
    beholder->sched->longitude = 70.73;

    parse_args(argc, argv,
               "port=%d", &port,
               "rem_host=%s", &rem_host,
               "rem_port=%d", &rem_port,
               "channel_host=%s", &channel_host,
               "channel_port=%d", &channel_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "lat=%lf", &beholder->sched->latitude,
               "lon=%lf", &beholder->sched->longitude,
               NULL);

    beholder->server = server_create();
    server_listen(beholder->server, "localhost", port);

    SERVER_SET_HOOK(beholder->server, line_read_hook, process_command, beholder);
    /* Set up custom network post-connection code */
    SERVER_SET_HOOK(beholder->server, connection_connected_hook, connection_connected, beholder);
    SERVER_SET_HOOK(beholder->server, connection_disconnected_hook, connection_disconnected, beholder);

    beholder->rem_connection = server_add_connection(beholder->server, rem_host, rem_port);
    beholder->rem_connection->is_active = TRUE;

    beholder->channel_connection = NULL;//server_add_connection(beholder->server, channel_host, channel_port);
    //beholder->channel_connection->is_active = TRUE;

    beholder->logger_connection = server_add_connection(beholder->server, logger_host, logger_port);
    beholder->logger_connection->is_active = TRUE;

    /* Try to guess correct initial state */
    beholder->is_night = is_night(beholder);
    beholder->is_open = FALSE;
    beholder->is_observing = FALSE;
    beholder->is_light = FALSE;
    beholder->is_below = FALSE;
    beholder->ra = 0;
    beholder->dec = 0;
    beholder->next_ra = 0;
    beholder->next_dec = 0;
    beholder->is_safe = FALSE;

    beholder->is_quit = FALSE;

    while(!beholder->is_quit)
        server_cycle(beholder->server, 1);

    scheduler_delete(beholder->sched);

    server_delete(beholder->server);

    free(beholder);

    return EXIT_SUCCESS;
}
