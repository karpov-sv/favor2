#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "command.h"
#include "command_queue.h"

#include "ports.h"

#define COUNTER_FINISHED 30

typedef struct mount_str {
    server_str *server;

    connection_str *beholder_connection;
    connection_str *grabber_connection;

    int id;

    /* 0 - idle, 1 - slewing, 2 - tracking */
    int state;
    int counter;

    int is_calibrated;

    double ra;
    double dec;

    command_queue_str *commands;

    int is_quit;
} mount_str;

void move_mount_cb(server_str *server, int type, void *ptr)
{
    mount_str *mount = (mount_str *)ptr;
    connection_str *conn;

    if(type == 0){
        /* repoint */
        if(mount->state == 1){
            mount->counter = MIN(mount->counter + 1, COUNTER_FINISHED);

            if(mount->counter == COUNTER_FINISHED){
                command_queue_done(mount->commands, "repoint");

                mount->state = 2;
                mount->counter = 0;
            } else
                server_add_timer(mount->server, 0.1, 0, move_mount_cb, mount);
        }
    } else if(type == 1){
        /* tracking_start */
        mount->state = 2;
        command_queue_done(mount->commands, "tracking_start");
    } else if(type == 2){
        /* tracking_stop */
        mount->state = 0;
        command_queue_done(mount->commands, "tracking_stop");
    }
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    mount_str *mount = (mount_str *)data;
    command_str *command = command_parse(string);

    /* server_default_line_read_hook(server, connection, string, data); */

    if(command_match(command, "exit") || command_match(command, "quit")){
        mount->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_mount_status")){
        server_connection_message(connection, "mount_status connected=1 state=%d id=%d ra=%g dec=%g calibrated=%d",
                                  mount->state, mount->id, mount->ra, mount->dec, mount->is_calibrated);
    } else if(command_match(command, "tracking_start")){
        if(TRUE || mount->state == 0){
            command_queue_add(mount->commands, connection, command_name(command), 10);
            server_add_timer(mount->server, 0.1, 1, move_mount_cb, mount);
        }
    } else if(command_match(command, "tracking_stop")){
        if(TRUE || mount->state == 2){
            command_queue_add(mount->commands, connection, command_name(command), 10);
            server_add_timer(mount->server, 0.1, 2, move_mount_cb, mount);
        }
    } else if(command_match(command, "repoint")){
        command_args(command,
                     "ra=%lf", &mount->ra,
                     "dec=%lf", &mount->dec,
                     NULL);

        mount->state = 1;

        server_connection_message(mount->grabber_connection, "set_pointing ra=%g dec=%g", mount->ra, mount->dec);

        command_queue_add(mount->commands, connection, command_name(command), 10.0);
    } else if(command_match(command, "fix")){
        command_args(command,
                     "ra=%lf", &mount->ra,
                     "dec=%lf", &mount->dec,
                     NULL);

        mount->is_calibrated = TRUE;
        server_connection_message(connection, "fix_done");
    } else if(command_match(command, "set_pointing_done")){
        mount->state = 2;
        command_queue_done(mount->commands, "repoint");
    } else if(command_match(command, "park")){
        server_connection_message(connection, "park_done");
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
    }

    if(connection == mount->grabber_connection){
        server_connection_message(connection, "get_fix");
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    mount_str *mount = (mount_str *)data;

    /* Remove from command queue all commands belonging to this connection, or
       else command_queue_done will fail trying to send the reply to it */
    /* TODO: BTW, maybe another solution would be to mark such "lost"
       connections as NULLs, and handle them specially later?.. */
    command_queue_remove_with_connection(mount->commands, connection);

    server_default_connection_disconnected_hook(server, connection, NULL);
}

int main(int argc, char **argv)
{
    mount_str *mount = (mount_str *)malloc(sizeof(mount_str));
    int port = PORT_MOUNT;
    char *beholder_host = "localhost";
    int beholder_port = PORT_BEHOLDER;
    char *grabber_host = "localhost";
    int grabber_port = PORT_GRABBER_CONTROL;

    mount->id = 0;

    parse_args(argc, argv,
               "port=%d", &port,
               "id=%d", &mount->id,
               "beholder_host=%s", &beholder_host,
               "beholder_port=%d", &beholder_port,
               "grabber_host=%s", &grabber_host,
               "grabber_port=%d", &grabber_port,
               NULL);

    mount->server = server_create();

    SERVER_SET_HOOK(mount->server, line_read_hook, process_command, mount);
    SERVER_SET_HOOK(mount->server, connection_connected_hook, connection_connected, mount);
    SERVER_SET_HOOK(mount->server, connection_disconnected_hook, connection_disconnected, mount);

    mount->beholder_connection = server_add_connection(mount->server, beholder_host, beholder_port);
    mount->beholder_connection->is_active = TRUE;
    mount->grabber_connection = server_add_connection(mount->server, grabber_host, grabber_port);
    mount->grabber_connection->is_active = TRUE;

    mount->counter = 0;
    mount->state = 0;

    mount->is_calibrated = FALSE;

    mount->ra = 0;
    mount->dec = 0;

    mount->commands = command_queue_create();

    mount->is_quit = FALSE;

    server_listen(mount->server, "localhost", port);

    while(!mount->is_quit){
        server_cycle(mount->server, 10);

        command_queue_check(mount->commands);
    }

    command_queue_delete(mount->commands);

    server_delete(mount->server);

    return EXIT_SUCCESS;
}
