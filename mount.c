#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "command.h"
#include "command_queue.h"
#include "coords.h"
#include "license.h"

#include "ports.h"

//#define MOUNT_DEBUG 1

/* Protocol is described here:
   - http://code.google.com/p/skywatcher/wiki/SkyWatcherProtocol
   - http://www.sharpcap.co.uk/supatrakmounthacking
*/


/* Full rotation around each axis */
#define EQ6_FULLCIRCLE 0x89B200
/* Values of encoders after reset */
#define EQ6_ZERO1 0x800000
/* #define EQ6_ZERO2 0xA26C80 */
#define EQ6_ZERO2 0x800000
/* Sidereal tracking speed */
#define EQ6_RATE_SIDEREAL 0x00026C

#define COMMAND_NONE -1
#define COMMAND_ECHO 0
#define COMMAND_GETSTATUS1 1
#define COMMAND_GETSTATUS2 2
#define COMMAND_GETPOSITION1 3
#define COMMAND_GETPOSITION2 4

#define COMMAND_STOP_MOTOR1 10
#define COMMAND_STOP_MOTOR2 11

#define COMMAND_START_MOTOR1 12
#define COMMAND_START_MOTOR2 13

#define COMMAND_RATE_SLEW_MOTOR1 14
#define COMMAND_RATE_SLEW_MOTOR2 15

#define COMMAND_RATE_INV_SLEW_MOTOR1 114
#define COMMAND_RATE_INV_SLEW_MOTOR2 115

#define COMMAND_RATE_SIDEREAL_MOTOR1 16

#define COMMAND_TARGET_MOTOR1 20
#define COMMAND_TARGET_MOTOR2 21

#define COMMAND_INIT_MOTOR1 30
#define COMMAND_INIT_MOTOR2 31

#define COMMAND_SPEED_MANUAL_MOTOR1 50
#define COMMAND_SPEED_MANUAL_MOTOR2 51

#define COMMAND_RATE_NORMAL_MOTOR1 60
#define COMMAND_RATE_NORMAL_MOTOR2 61
#define COMMAND_RATE_INV_NORMAL_MOTOR1 62
#define COMMAND_RATE_INV_NORMAL_MOTOR2 63

#define STATUS_ERROR -1
#define STATUS_IDLE 0
#define STATUS_SLEWING 1
#define STATUS_TRACKING 2
#define STATUS_TRACKING_PREPARE 3
#define STATUS_MANUAL 4
#define STATUS_UNKNOWN 100

typedef struct mount_str {
    server_str *server;

    connection_str *beholder_connection;
    connection_str *lowlevel_connection;
    connection_str *logger_connection;

    int id;

    int state;

    int is_calibrated;

    /* Computed RA/Dec */
    double ra;
    double dec;

    /* Computed Alt/Az */
    double alt;
    double az;

    /* Values of encoders for RA/Dec axes */
    double enc_ra;
    double enc_dec;

    /* Differences between zero encoder positions and zero equatorial position */
    double ra0;
    double dec0;

    /* Target sky position */
    double next_ra;
    double next_dec;

    /* Target encoder positions */
    double next_enc_ra;
    double next_enc_dec;

    /* Motor states */
    int motor_is_moving1;
    int motor_is_moving2;
    int motor_direction1;
    int motor_direction2;

    command_queue_str *commands;

    int *fifo;
    int fifo_length;
    int fifo_maxlength;

    int current_command;
    time_str current_command_time;

    /* Mount location */
    double latitude;
    double longitude;

    time_str last_status_time;

    int is_quit;
} mount_str;

void mount_init(mount_str *mount);

void mount_log(mount_str *mount, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    if(mount->logger_connection)// && logger->logger_connection->is_connected)
        server_connection_message(mount->logger_connection, buffer);

    free(buffer);
}

void mount_fifo_add(mount_str *mount, int command)
{
    if(mount->fifo_maxlength <= mount->fifo_length){
        mount->fifo_maxlength ++;
        mount->fifo = realloc(mount->fifo, sizeof(int)*mount->fifo_maxlength);
    }

    mount->fifo[mount->fifo_length] = command;
    mount->fifo_length ++;

#ifdef MOUNT_DEBUG
    dprintf("FIFO: added command %d\n", command);
#endif /* MOUNT_DEBUG */
}

inline int mount_fifo_next(mount_str *mount)
{
    if(!mount->fifo_length)
        return -1;
    else
        return mount->fifo[0];
}

void mount_fifo_shift(mount_str *mount)
{
    if(mount->fifo_length){
        memmove(mount->fifo, mount->fifo + 1, sizeof(int)*(mount->fifo_length - 1));
        mount->fifo_length --;
    } else
        dprintf("Trying to shift empty FIFO\n");
}

void mount_set_motor_target(mount_str *mount, int id, double value)
{
    int zero = (id == 1 ? EQ6_ZERO1 : EQ6_ZERO2);
    int encoder = value/360.0*EQ6_FULLCIRCLE + zero;

#ifdef MOUNT_DEBUG
    dprintf("%g -> %06X\n", value, encoder);
    dprintf("%02X %02X %02X\n", encoder & 0xff, (encoder >> 8) & 0xff, (encoder >> 16) & 0xff);
#endif /* MOUNT_DEBUG */

    server_connection_message(mount->lowlevel_connection, ":S%c%02X%02X%02X\r", id == 1 ? '1' : '2',
                              encoder & 0xff, (encoder >> 8) & 0xff, (encoder >> 16) & 0xff);
}

void mount_fifo_check(mount_str *mount)
{
    int next = mount_fifo_next(mount);

    if(mount->current_command < 0 && next >= 0){
        mount->current_command = next;
        mount->current_command_time = time_current();

        mount_fifo_shift(mount);

        switch(mount->current_command){
        case COMMAND_INIT_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":F1\r");
            break;
        case COMMAND_INIT_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":F2\r");
            break;
        case COMMAND_GETSTATUS1:
            server_connection_message(mount->lowlevel_connection, ":f1\r");
            break;
        case COMMAND_GETSTATUS2:
            server_connection_message(mount->lowlevel_connection, ":f2\r");
            break;
        case COMMAND_GETPOSITION1:
            server_connection_message(mount->lowlevel_connection, ":j1\r");
            break;
        case COMMAND_GETPOSITION2:
            server_connection_message(mount->lowlevel_connection, ":j2\r");
            break;

        case COMMAND_STOP_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":L1\r");
            break;
        case COMMAND_STOP_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":L2\r");
            break;
        case COMMAND_START_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":J1\r");
            break;
        case COMMAND_START_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":J2\r");
            break;

        case COMMAND_RATE_SLEW_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":G100\r");
            break;
        case COMMAND_RATE_SLEW_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":G200\r");
            break;

        case COMMAND_RATE_INV_SLEW_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":G101\r");
            break;
        case COMMAND_RATE_INV_SLEW_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":G201\r");
            break;

        case COMMAND_RATE_NORMAL_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":G130\r");
            break;
        case COMMAND_RATE_NORMAL_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":G230\r");
            break;
        case COMMAND_RATE_INV_NORMAL_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":G131\r");
            break;
        case COMMAND_RATE_INV_NORMAL_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":G231\r");
            break;
        case COMMAND_SPEED_MANUAL_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":I1220000\r");
            break;
        case COMMAND_SPEED_MANUAL_MOTOR2:
            server_connection_message(mount->lowlevel_connection, ":I2220000\r");
            break;


        case COMMAND_RATE_SIDEREAL_MOTOR1:
            server_connection_message(mount->lowlevel_connection, ":G110\r");
            break;

        case COMMAND_TARGET_MOTOR1:
            mount_set_motor_target(mount, 1, mount->next_enc_ra);
            break;
        case COMMAND_TARGET_MOTOR2:
            mount_set_motor_target(mount, 2, mount->next_enc_dec);
            break;

        default:
            mount->current_command = -1;
        }
    } else if(mount->current_command >= 0 && 1e-3*time_interval(mount->current_command_time, time_current()) > 5){
        /* TODO: repeat the command */
        dprintf("Timeout on command: %d\n", mount->current_command);
        /* If we have no reply for status command, let's call it an error */
        /* if(mount->current_command == COMMAND_ISGOTO || mount->current_command == COMMAND_GETRADEC) */
        /*    mount->status = STATUS_ERROR; */
        mount->current_command = -1;
        mount_init(mount);
    }
}

int mount_fifo_contains(mount_str *mount, int command)
{
    int d;

    if(mount->current_command == command)
        return TRUE;

    for(d = 0; d < mount->fifo_length; d++)
        if(mount->fifo[d] == command)
            return TRUE;

    return FALSE;
}

void mount_init(mount_str *mount)
{
    mount_fifo_add(mount, COMMAND_INIT_MOTOR1);
    mount_fifo_add(mount, COMMAND_INIT_MOTOR2);
}

void mount_request_state_cb(server_str *server, int type, void *data)
{
    mount_str *mount = (mount_str *)data;

    if(mount->fifo_length == 0){
        mount_fifo_add(mount, COMMAND_GETSTATUS1);
        mount_fifo_add(mount, COMMAND_GETSTATUS2);

        mount_fifo_add(mount, COMMAND_GETPOSITION1);
        mount_fifo_add(mount, COMMAND_GETPOSITION2);
    }

    server_add_timer(server, 0.1, 0, mount_request_state_cb, mount);
}

void mount_repoint(mount_str *mount, double enc_ra, double enc_dec)
{
    mount_log(mount, "Repointing started");

    mount->next_enc_ra = enc_ra;
    mount->next_enc_dec = enc_dec;

    /* TODO: should convert all the angles to reasonable range */
    while(mount->next_enc_ra > 180)
        mount->next_enc_ra -= 360;
    while(mount->next_enc_ra < -180)
        mount->next_enc_ra += 360;

    while(mount->next_enc_dec > 180)
        mount->next_enc_dec -= 360;
    while(mount->next_enc_dec < -180)
        mount->next_enc_dec += 360;

    mount_fifo_add(mount, COMMAND_STOP_MOTOR1);
    mount_fifo_add(mount, COMMAND_RATE_SLEW_MOTOR1);
    mount_fifo_add(mount, COMMAND_TARGET_MOTOR1);
    mount_fifo_add(mount, COMMAND_START_MOTOR1);

    mount_fifo_add(mount, COMMAND_STOP_MOTOR2);
    mount_fifo_add(mount, COMMAND_RATE_SLEW_MOTOR2);
    mount_fifo_add(mount, COMMAND_TARGET_MOTOR2);
    mount_fifo_add(mount, COMMAND_START_MOTOR2);

    mount->state = STATUS_SLEWING;
}

void mount_repoint_ra_dec(mount_str *mount, double ra, double dec)
{
    mount_log(mount, "Will repoint to RA=%g Dec=%g", ra, dec);

    /* RA of objects crossing the meridian */
    double ra0 = 15.0*time_str_get_local_sidereal_time(time_current(), mount->longitude);

    if(sin(ra0*M_PI/180)*sin(ra*M_PI/180) + cos(ra0*M_PI/180)*cos(ra*M_PI/180) < 0){
        /* Pointing north, let's go through pole, not around */
        mount_log(mount, "Target is on the North");
        ra = ra - 180;
        dec = 90 - (dec - 90);

        while(ra < 0)
            ra += 360;
        while(ra > 360)
            ra -= 360;
    }

    double enc_ra = ra0 - ra - mount->ra0;
    double enc_dec = -dec - mount->dec0;

    mount_repoint(mount, enc_ra, enc_dec);
}

void mount_park(mount_str *mount)
{
    mount_log(mount, "Parking started");

    if(mount->is_calibrated){
        /* RA, Dec of Zenith */
        double ra0 = 15.0*time_str_get_local_sidereal_time(time_current(), mount->longitude);
        double dec0 = mount->latitude;

        dec0 = dec0 - 15;

        mount_repoint_ra_dec(mount, ra0, dec0);
    } else
        mount_repoint(mount, 0, 0);

    mount->state = STATUS_SLEWING;
}

void mount_tracking_start(mount_str *mount)
{
    mount_log(mount, "Starting tracking");

    mount_fifo_add(mount, COMMAND_STOP_MOTOR2);
    mount_fifo_add(mount, COMMAND_STOP_MOTOR1);
    mount_fifo_add(mount, COMMAND_RATE_SIDEREAL_MOTOR1);
    mount_fifo_add(mount, COMMAND_START_MOTOR1);

    mount->state = STATUS_TRACKING_PREPARE;
}

void mount_tracking_stop(mount_str *mount)
{
    mount_log(mount, "Stopping tracking");

    mount_fifo_add(mount, COMMAND_STOP_MOTOR2);
    mount_fifo_add(mount, COMMAND_STOP_MOTOR1);
}

void mount_stop(mount_str *mount)
{
    mount_log(mount, "Stopping mount motors");

    mount_fifo_add(mount, COMMAND_STOP_MOTOR1);
    mount_fifo_add(mount, COMMAND_STOP_MOTOR2);

    mount->state = STATUS_IDLE;
}

void mount_set_motors(mount_str *mount, int m1, int m2)
{
    mount_log(mount, "Manual control of motors: %d %d", m1, m2);

    mount_fifo_add(mount, COMMAND_STOP_MOTOR1);
    mount_fifo_add(mount, COMMAND_STOP_MOTOR2);

    if(m1 != 0){
        mount_fifo_add(mount, m1 > 0 ? COMMAND_RATE_NORMAL_MOTOR1 : COMMAND_RATE_INV_NORMAL_MOTOR1);
        mount_fifo_add(mount, COMMAND_SPEED_MANUAL_MOTOR1);
        mount_fifo_add(mount, COMMAND_START_MOTOR1);
    }

    if(m2 != 0){
        mount_fifo_add(mount, m2 > 0 ? COMMAND_RATE_NORMAL_MOTOR2 : COMMAND_RATE_INV_NORMAL_MOTOR2);
        mount_fifo_add(mount, COMMAND_SPEED_MANUAL_MOTOR2);
        mount_fifo_add(mount, COMMAND_START_MOTOR2);
    }

    if(m1 != 0 || m2 != 0)
        mount->state = STATUS_MANUAL;
    else
        mount->state = STATUS_IDLE;
}

static double parse_position(char *data)
{
    int p1 = 0;
    int p2 = 0;
    int p3 = 0;

    sscanf(data, "%02x%02x%02x", &p1, &p2, &p3);

    return (p3*256 + p2)*256 + p1;
}

void mount_update_status(mount_str *mount)
{
    if(!mount->motor_is_moving1 && !mount->motor_is_moving2){
        if(mount->state == STATUS_SLEWING){
            if(command_queue_done(mount->commands, "repoint"))
                mount_tracking_start(mount);
            else
                command_queue_done(mount->commands, "park");
        } else if(mount->state == STATUS_TRACKING)
            /* FIXME: we may stop due to some error */
            command_queue_done(mount->commands, "tracking_stop");
        if(mount->state != STATUS_TRACKING_PREPARE && mount->state != STATUS_MANUAL)
            mount->state = STATUS_IDLE;
    } else if(mount->motor_is_moving1 && !mount->motor_is_moving2 && mount->motor_direction1){
        if(mount->state == STATUS_TRACKING_PREPARE){
            mount->state = STATUS_TRACKING;
            command_queue_done(mount->commands, "tracking_start");
        }
    }
}

void mount_check_safety(mount_str *mount)
{
    return;

    /* FIXME: these limits should be checked and adjusted, and similar ones for Dec also included here */
    if((mount->enc_ra < -90 && mount->motor_direction1 < 0) ||
       (mount->enc_ra > 90 && mount->motor_direction1 > 0)){
        if(mount->state == STATUS_SLEWING){
            command_queue_failure(mount->commands, "repoint");
        }

        mount_stop(mount);
    }
}

void mount_update_ra_dec(mount_str *mount)
{
    /* RA of objects crossing the meridian */
    double ra0 = 15.0*time_str_get_local_sidereal_time(time_current(), mount->longitude);

    mount->ra = ra0 - mount->enc_ra - mount->ra0;
    mount->dec = -mount->enc_dec - mount->dec0;

    if(mount->dec > 90){
        mount->dec = 90 - (mount->dec - 90);
        mount->ra = mount->ra - 180;
    }

    while(mount->ra < 0)
        mount->ra += 360;
    while(mount->ra > 360)
        mount->ra -= 360;

    /* Update alt/az */
    {
        double ha = ra0 - mount->ra;

        mount->alt = 180.0/M_PI*asin(sin(mount->dec*M_PI/180.0)*sin(mount->latitude*M_PI/180.0) + cos(mount->dec*M_PI/180.0)*cos(mount->latitude*M_PI/180.0)*cos(ha*M_PI/180.0));

        mount->az = 180.0/M_PI*acos((sin(mount->dec*M_PI/180.0) - sin(mount->alt*M_PI/180.0)*sin(mount->latitude*M_PI/180.0))/cos(mount->alt*M_PI/180.0)/cos(mount->latitude*M_PI/180.0));

        if(sin(ha*M_PI/180.0) > 0)
            mount->az = 360.0 - mount->az;
    }
}

void mount_fix_store(mount_str *mount)
{
    char *filename = "mount.fix";
    FILE *file = fopen(filename, "w");

    if(file){
        fprintf(file, "fix %g %g\n", mount->ra0, mount->dec0);

        dprintf("Mount calibration stored to %s\n", filename);

        fclose(file);
    }
}

void mount_fix_restore(mount_str *mount)
{
    char *filename = "mount.fix";
    FILE *file = fopen(filename, "r");

    if(file){
        if(fscanf(file, "fix %lf %lf\n", &mount->ra0, &mount->dec0) == 2)
            dprintf("Mount calibration restored from %s\n", filename);

        mount->is_calibrated = TRUE;
        fclose(file);
    }
}

void mount_fix_ra_dec(mount_str *mount, double ra, double dec)
{
    mount_log(mount, "Fixing position: RA=%g Dec=%g", ra, dec);

    mount_update_ra_dec(mount);

    /* TODO: it should be checked */
    mount->ra0 -= ra - mount->ra;
    mount->dec0 -= dec - mount->dec;

    /* Mark the mount as 'calibrated' */
    mount->is_calibrated = TRUE;
}

int process_mount_data(server_str *server, connection_str *conn, char *data, int length, void *ptr)
{
    mount_str *mount = (mount_str *)ptr;
    int processed = 0;

#ifdef MOUNT_DEBUG
    dprintf("Mount (%d): %s\n", length, make_hex_string(data, length));
#endif /* MOUNT_DEBUG */

    if(length && (data[0] == '=' || data[0] == '!')){
        int endpos = 0;

        /* Let's find the end of reply */
        while(*(data + endpos) != '\r'){
            if(endpos >= length){
                endpos = 0;
                break;
            } else
                endpos ++;
        }

        if(endpos){
            processed = endpos + 1;

            if(mount->current_command == COMMAND_GETSTATUS1 && endpos == 4){
                mount->motor_is_moving1 = (data[2] == '1');
                mount->motor_direction1 = ((data[1] & 0x2) ? -1 : 1);
                mount->last_status_time = time_current();
            } else if(mount->current_command == COMMAND_GETSTATUS2 && endpos == 4){
                mount->motor_is_moving2 = (data[2] == '1');
                mount->motor_direction2 = ((data[1] & 0x2) ? -1 : 1);

                mount_update_status(mount);
            } else if(mount->current_command == COMMAND_GETPOSITION1 && endpos == 7){
                mount->enc_ra = 360.0*(parse_position(data + 1) - EQ6_ZERO1)/EQ6_FULLCIRCLE;
            } else if(mount->current_command == COMMAND_GETPOSITION2 && endpos == 7){
                mount->enc_dec = 360.0*(parse_position(data + 1) - EQ6_ZERO2)/EQ6_FULLCIRCLE;

                mount_check_safety(mount);
            } else if(mount->current_command == COMMAND_INIT_MOTOR2 && endpos == 1)
                command_queue_done(mount->commands, "init");

            mount->current_command = -1;
        }
    } else if(length)
        processed = 1;

    if(processed && processed < length)
        processed += process_mount_data(server, conn, data + processed, length - processed, mount);

    return processed;
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    mount_str *mount = (mount_str *)data;
    command_str *command = command_parse(string);

    //server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        mount->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_mount_status")){
        mount_update_ra_dec(mount);
        server_connection_message(connection, "mount_status state=%d id=%d calibrated=%d "
                                  "connected=%d "
                                  "ra=%g dec=%g next_ra=%g next_dec=%g "
                                  "enc_ra=%g enc_dec=%g next_enc_ra=%g next_enc_dec=%g "
                                  "ra0=%g dec0=%g alt=%g az=%g",
                                  mount->state, mount->id, mount->is_calibrated,
                                  mount->lowlevel_connection->is_connected && 1e-3*time_interval(mount->last_status_time, time_current()) < 1,
                                  mount->ra, mount->dec, mount->next_ra, mount->next_dec,
                                  mount->enc_ra, mount->enc_dec, mount->next_enc_ra, mount->next_enc_dec,
                                  mount->ra0, mount->dec0, mount->alt, mount->az);
    } else if(command_match(command, "init")){
        mount_init(mount);
        command_queue_add(mount->commands, connection, command_name(command), 10);
    } else if(command_match(command, "tracking_start")){
        mount_tracking_start(mount);
        if(mount->state != STATUS_TRACKING)
            command_queue_add(mount->commands, connection, command_name(command), 10);
        else
            server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "tracking_stop")){
        mount_tracking_stop(mount);
        if(mount->state == STATUS_TRACKING)
            command_queue_add(mount->commands, connection, "tracking_stop", 10);
        else
            server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "repoint")){
        command_args(command,
                     "ra=%lf", &mount->next_ra,
                     "dec=%lf", &mount->next_dec,
                     NULL);

        mount_repoint_ra_dec(mount, mount->next_ra, mount->next_dec);

        command_queue_add(mount->commands, connection, command_name(command), 60);
    } else if(command_match(command, "fix")){
        double ra = 0;
        double dec = 0;

        command_args(command,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     NULL);

        mount_fix_ra_dec(mount, ra, dec);

        mount_fix_store(mount);

        server_connection_message(connection, "fix_done");
    } else if(command_match(command, "park")){
        mount_park(mount);

        command_queue_add(mount->commands, connection, command_name(command), 60);
    } else if(command_match(command, "stop")){
        mount_stop(mount);

        server_connection_message(connection, "stop_done");
    } else if(command_match(command, "set_motors")){
        int m1 = 0;
        int m2 = 0;

        command_args(command,
                     "motor1=%d", &m1,
                     "motor2=%d", &m2,
                     NULL);

        mount_set_motors(mount, m1, m2);

        server_connection_message(connection, "set_motors_done");
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    mount_str *mount = (mount_str *)data;

    if(connection == mount->beholder_connection){
        server_connection_message(connection, "iam class=mount id=%d", mount->id);
    } else if(connection == mount->lowlevel_connection)
        mount_log(mount, "HW controller connected");
    else if(connection == mount->logger_connection){
        server_connection_message(connection, "iam id=mount%d", mount->id);
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    mount_str *mount = (mount_str *)data;

    if(connection == mount->lowlevel_connection)
        mount_log(mount, "HW controller disconnected");

    command_queue_remove_with_connection(mount->commands, connection);

    server_default_connection_connected_hook(server, connection, NULL);
}

int main(int argc, char **argv)
{
    mount_str *mount = (mount_str *)malloc(sizeof(mount_str));
    int port = PORT_MOUNT;
    char *lowlevel_host = "localhost";
    int lowlevel_port = PORT_MOUNT_LOWLEVEL;
    char *beholder_host = "localhost";
    int beholder_port = PORT_BEHOLDER;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;

    mount->id = 0;

    mount->ra0 = 0;
    mount->dec0 = 0;

    mount->latitude = 43.65722222;
    mount->longitude = -41.43235417;

    parse_args(argc, argv,
               "port=%d", &port,
               "id=%d", &mount->id,
               "lowlevel_host=%s", &lowlevel_host,
               "lowlevel_port=%d", &lowlevel_port,
               "beholder_host=%s", &beholder_host,
               "beholder_port=%d", &beholder_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "ra0=%lf", &mount->ra0,
               "dec0=%lf", &mount->dec0,
               "lat=%lf", &mount->latitude,
               "lon=%lf", &mount->longitude,
               NULL);

    check_license(argv[0]);

    mount->server = server_create();

    SERVER_SET_HOOK(mount->server, line_read_hook, process_command, mount);
    SERVER_SET_HOOK(mount->server, connection_connected_hook, connection_connected, mount);
    SERVER_SET_HOOK(mount->server, connection_disconnected_hook, connection_disconnected, mount);

    mount->beholder_connection = server_add_connection(mount->server, beholder_host, beholder_port);
    mount->beholder_connection->is_active = TRUE;

    mount->logger_connection = server_add_connection(mount->server, logger_host, logger_port);
    mount->logger_connection->is_active = TRUE;

    mount->lowlevel_connection = server_add_connection(mount->server, lowlevel_host, lowlevel_port);
    mount->lowlevel_connection->is_active = TRUE;
    mount->lowlevel_connection->data_read_hook = process_mount_data;
    mount->lowlevel_connection->data_read_hook_data = mount;
    server_add_timer(mount->server, 0.1, 0, mount_request_state_cb, mount);

    mount->state = STATUS_UNKNOWN;

    mount->is_calibrated = FALSE;

    mount->commands = command_queue_create();

    mount->fifo = NULL;
    mount->fifo_length = 0;
    mount->fifo_maxlength = 0;

    mount->ra = 0;
    mount->dec = 0;

    mount->alt = 0;
    mount->az = 0;

    mount->enc_ra = 0;
    mount->enc_dec = 0;

    mount->next_ra = 0;
    mount->next_dec = 0;

    mount->next_enc_ra = 0;
    mount->next_enc_dec = 0;

    mount->motor_is_moving1 = 0;
    mount->motor_is_moving2 = 0;
    mount->motor_direction1 = 0;
    mount->motor_direction2 = 0;

    mount->current_command = -1;
    mount->current_command_time = time_current();

    mount->last_status_time = time_zero();

    mount->is_quit = FALSE;

    server_listen(mount->server, "localhost", port);

    mount_fix_restore(mount);

    mount_init(mount);

    mount_log(mount, "Mount started");

    while(!mount->is_quit){
        server_cycle(mount->server, 10);

        command_queue_check(mount->commands);

        mount_fifo_check(mount);
    }

    mount_log(mount, "Mount stopped");

    command_queue_delete(mount->commands);

    server_delete(mount->server);

    return EXIT_SUCCESS;
}
