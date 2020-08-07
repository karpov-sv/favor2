#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "command.h"

#include "ports.h"

typedef struct weather_str {
    server_str *server;

    int status;

    int cloud_cond;
    int rain_cond;
    int wind_cond;

    int is_quit;
} weather_str;

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    weather_str *weather = (weather_str *)data;
    command_str *command = command_parse(string);

    server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        weather->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_weather_status")){
        server_connection_message(connection, "weather_status cloud_cond=%d rain_cond=%d wind_cond=%d",
                                  weather->cloud_cond, weather->rain_cond, weather->wind_cond);
    } else if(command_match(command, "set_weather")){
        command_args(command,
                     "cloud_cond=%d", &weather->cloud_cond,
                     "rain_cond=%d", &weather->rain_cond,
                     "wind_cond=%d", &weather->wind_cond,
                     NULL);
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

int main(int argc, char **argv)
{
    weather_str *weather = (weather_str *)malloc(sizeof(weather_str));
    int port = PORT_WEATHER;

    parse_args(argc, argv,
               "port=%d", &port,
               NULL);

    weather->server = server_create();

    SERVER_SET_HOOK(weather->server, line_read_hook, process_command, weather);

    weather->cloud_cond = 1;
    weather->rain_cond = 1;
    weather->wind_cond = 1;

    weather->is_quit = FALSE;

    server_listen(weather->server, "localhost", port);

    while(!weather->is_quit)
        server_cycle(weather->server, 10);

    server_delete(weather->server);

    return EXIT_SUCCESS;
}
