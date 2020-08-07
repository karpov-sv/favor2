#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "image_udp.h"
#include "command.h"
#include "command_queue.h"
#include "license.h"

#include "ports.h"

//#define CAN_DEBUG 1

typedef struct chiller_str {
    int id;

    int state;

    double t_cold;
    double t_hot;
    double t_chip;

    time_str last_status_time;
} chiller_str;

typedef struct can_str {
    server_str *server;

    connection_str *lowlevel_connection;
    connection_str *logger_connection;

    command_queue_str *commands;

    int nchillers;
    chiller_str *chillers;

    time_str last_time;

    int is_quit;
} can_str;

void can_log(can_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));

void can_log(can_str *can, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    if(can->logger_connection)// && can->logger_connection->is_connected)
        server_connection_message(can->logger_connection, buffer);

    free(buffer);
}

void can_send_command(can_str *can, unsigned int dest, int type)
{
    int cop = 0x10;
    char data[2];

    if(dest > 0xf)
        dest = 0xf;

    data[0] = (type & 0xf) << 4;
    data[1] = 0;

#ifdef CAN_DEBUG
    dprintf("sending: t%02X%1X2%02X%02X\xd\n", cop, dest, data[0], data[1]);
#endif

    server_connection_message(can->lowlevel_connection, "t%02X%1X2%02X%02X\xd", cop, dest, data[0], data[1]);
}

void can_request_state_cb(server_str *server, int type, void *data)
{
    can_str *can = (can_str *)data;
    static int id = 0;
    static int mode = 0;

    if(can->lowlevel_connection->is_connected){
        double age = 1e-3*time_interval(can->chillers[id].last_status_time, time_current());

        if(age > 1.0){
            if(mode == 0)
                can_send_command(can, id + 1, 2); /* Request the status */
            else if(mode == 1)
                can_send_command(can, id + 1, 6); /* Request the temperature */
#ifdef CAN_DEBUG
            dprintf("Requesting state: %d mode=%d\n", id+1, mode);
#endif
        }

        if(age > 5.0)
            can->chillers[id].state = -1;

        if(1e-3*time_interval(can->last_time, time_current()) > 300.0)
            /* No CAN packets seen in 10 seconds, probably the connection is lost */
            server_connection_restart(can->server, can->lowlevel_connection);
    }

    id ++;
    if(id >= can->nchillers){
        id = 0;

        mode ++;
        if(mode > 1)
            mode = 0;
    }

    server_add_timer(server, 0.1, 0, can_request_state_cb, can);
}

static void process_can_data(server_str *server, connection_str *conn, char *string, void *data)
{
    can_str *can = (can_str *)data;
    int cop = 0;
    int addr = 0;
    int len = 0;

#ifdef CAN_DEBUG
    dprintf("CAN: %s\n", string);
#endif

    can->last_time = time_current();

    /* Sanity check */
    if(!string || string[0] != 't' || strlen(string) < 5)
        return;

    /* Parse the CAN packet */
    if(sscanf(string, "t%02x%1x%1x", &cop, &addr, &len)){
        char *dptr = string + 5;
        char data[len];
        int d;

        for(d = 0; d < len; d++){
            unsigned int t = 0;

            sscanf(dptr + 2*d, "%02x", &t);
            data[d] = t;
        }

#ifdef CAN_DEBUG
        {
            char *str = make_hex_string(data, len);
            dprintf("cop %02X addr %d len %d data: %s\n", cop, addr, len, str);

            free(str);
        }
#endif

        /* Analyze the packet */
        switch(cop){
        case 0x22:
            {
                int id = (data[0] & 0xf);
                int state = data[1];

                if(state & 0x01)
                    state = 0x01; /* Turned off - all other bits are undefined */

                can->chillers[id - 1].state = state;
                can->chillers[id - 1].last_status_time = time_current();
                break;
            }
        case 0x26:
            {
                int id = (data[0] & 0xf);
                int t_cold = *((int16_t*)(data + 1));
                int t_hot = *((int16_t*)(data + 3));
                int t_chip = *((int16_t*)(data + 5));


                can->chillers[id - 1].t_cold = 1e-2*t_cold;
                can->chillers[id - 1].t_hot = 1e-2*t_hot;
                can->chillers[id - 1].t_chip = 1e-2*t_chip;

#ifdef CAN_DEBUG
                dprintf("temperature %g %g %g\n", 1e-2*t_cold, 1e-2*t_hot, 1e-2*t_chip);
#endif
            }
        default:
            break;
        }
    }
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    can_str *can = (can_str *)data;
    command_str *command = command_parse(string);

    //server_default_line_read_hook(server, connection, string, data);

    if(connection == can->lowlevel_connection)
        return process_can_data(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        can->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_can_status")){
        char *status = make_string("can_status connected=%d chillers_state=1 nchillers=%d",
                                   can->lowlevel_connection->is_connected, can->nchillers);
        int d;

        for(d = 0; d < can->nchillers; d++){
            add_to_string(&status, " chiller%d_state=%d", can->chillers[d].id, can->chillers[d].state);
            add_to_string(&status, " chiller%d_t_cold=%g", can->chillers[d].id, can->chillers[d].t_cold);
            add_to_string(&status, " chiller%d_t_hot=%g", can->chillers[d].id, can->chillers[d].t_hot);
            add_to_string(&status, " chiller%d_t_chip=%g", can->chillers[d].id, can->chillers[d].t_chip);
        }

        server_connection_message(connection, status);

        free(status);
    } else if(command_match(command, "set_chillers")){
        int power = -1;

        command_args(command,
                     "power=%d", &power,
                     NULL);

        if(power == 0)
            can_send_command(can, 0xF, 0);
        else if(power > 0)
            can_send_command(can, 0xF, 1);

    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    can_str *can = (can_str *)data;

    if(connection == can->logger_connection){
        server_connection_message(connection, "iam id=can");
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    can_str *can = (can_str *)data;

    command_queue_remove_with_connection(can->commands, connection);

    server_default_connection_connected_hook(server, connection, NULL);
}

int main(int argc, char **argv)
{
    can_str *can = (can_str *)malloc(sizeof(can_str));
    char *lowlevel_host = "localhost";
    int lowlevel_port = PORT_CAN_LOWLEVEL;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;
    int port = PORT_CANSERVER;
    int d;

    can->nchillers = 10;

    parse_args(argc, argv,
               "lowlevel_host=%s", &lowlevel_host,
               "lowlevel_port=%d", &lowlevel_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "port=%d", &port,
               "nchillers=%d", &can->nchillers,
               NULL);

    check_license(argv[0]);

    can->server = server_create();
    can->is_quit = FALSE;

    can->chillers = malloc(sizeof(chiller_str)*can->nchillers);
    for(d = 0; d < can->nchillers; d++){
        can->chillers[d].id = d + 1;
        can->chillers[d].state = 0;
        can->chillers[d].t_cold = 0;
        can->chillers[d].t_hot = 0;
        can->chillers[d].t_chip = 0;
        can->chillers[d].last_status_time = time_zero();
    }

    can->lowlevel_connection = server_add_connection(can->server, lowlevel_host, lowlevel_port);
    can->lowlevel_connection->is_active = TRUE;
    //can->lowlevel_connection->data_read_hook = process_can_data;
    //can->lowlevel_connection->data_read_hook_data = can;
    server_add_timer(can->server, 0.1, 0, can_request_state_cb, can);

    can->logger_connection = server_add_connection(can->server, logger_host, logger_port);
    can->logger_connection->is_active = TRUE;

    SERVER_SET_HOOK(can->server, line_read_hook, process_command, can);
    SERVER_SET_HOOK(can->server, connection_connected_hook, connection_connected, can);
    SERVER_SET_HOOK(can->server, connection_disconnected_hook, connection_disconnected, can);

    server_listen(can->server, "localhost", port);

    can->commands = command_queue_create();

    can->last_time = time_current();

    while(!can->is_quit){
        server_cycle(can->server, 1);
        /* Check for sent commands with expired timers */
        command_queue_check(can->commands);
    }

    command_queue_delete(can->commands);
    server_delete(can->server);
    free(can);

    return EXIT_SUCCESS;
}
