#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "command.h"

#include "ports.h"

#define STATUS_OPENED 100

typedef struct dome_str {
    server_str *server;

    /* 0 - closed, 1 - opening, 2 - closing, 3 - open */
    int status;
    int pos;

    int is_quit;
} dome_str;

void move_dome_cb(server_str *server, int type, void *ptr)
{
    dome_str *dome = (dome_str *)ptr;

    if(dome->status == 1){
        dome->pos = MIN(dome->pos + 1, STATUS_OPENED);

        if(dome->pos == STATUS_OPENED){
            connection_str *conn;

            foreach(conn, server->connections)
                server_connection_message(conn, "open_dome_done");

            dome->status = 3;
        } else
            server_add_timer(dome->server, 0.1, 0, move_dome_cb, dome);
    } else {
        dome->pos = MAX(dome->pos - 1, 0);

        if(dome->pos == 0){
            connection_str *conn;

            foreach(conn, server->connections)
                server_connection_message(conn, "close_dome_done");

            dome->status = 0;
        } else
            server_add_timer(dome->server, 0.1, 0, move_dome_cb, dome);
    }
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    dome_str *dome = (dome_str *)data;
    command_str *command = command_parse(string);

    server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        dome->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_dome_status")){
        server_connection_message(connection, "dome_status state=%d",
                                  dome->status);
    } else if(command_match(command, "open_dome")){
        if(dome->status != 1){
            dome->status = 1;
            server_add_timer(dome->server, 0.1, 0, move_dome_cb, dome);
        }
    } else if(command_match(command, "close_dome")){
        if(dome->status != 2){
            dome->status = 2;
            server_add_timer(dome->server, 0.1, 0, move_dome_cb, dome);
        }
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

int main(int argc, char **argv)
{
    dome_str *dome = (dome_str *)malloc(sizeof(dome_str));
    int port = PORT_DOME;

    parse_args(argc, argv,
               "port=%d", &port,
               NULL);

    dome->server = server_create();

    SERVER_SET_HOOK(dome->server, line_read_hook, process_command, dome);

    dome->pos = 0;
    dome->status = 0;

    dome->is_quit = FALSE;

    server_listen(dome->server, "localhost", port);

    while(!dome->is_quit)
        server_cycle(dome->server, 10);

    server_delete(dome->server);

    return EXIT_SUCCESS;
}
