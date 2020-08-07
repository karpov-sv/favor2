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

    /* 0 - closed, 1 - opening, 2 - closing, 3 - open, 4 - unknown */
    int state;

    int dome_state;
    int dome_result;

    int dehum_channels;
    int dehum_dome;
    int vent_engineering;

    int remote;

    int sidewall;
    int handrail;

    int roof_s0;
    int roof_s1;

    connection_str *lowlevel_connection;

    int is_quit;
} dome_str;

int process_dome_data(server_str *server, connection_str *conn, char *data, int length, void *ptr)
{
    dome_str *dome = (dome_str *)ptr;
    int processed = 0;
    int error = 0;

    int check_crc(int len)
    {
        return crc8((unsigned char *)data, len) == *(u_int8_t*)(data + len) && !error;
    }

    if(!length)
        return processed;

    if(*data != '*'){
        //dprintf("Unknown reply char encountered!\n");
        processed = 1;
    } else if(length > 3){
        int type = *((u_int8_t *)(data + 1));

        error = *((u_int8_t *)(data + 2));

        //dprintf("reply %X, length %d, error %d\n", type, length, error);
        processed = 4;

        switch(type){
        case 0xB3:
            if(length > 5 && check_crc(5)){
                int state = *((u_int8_t *)(data + 3));
                int result = *((u_int8_t *)(data + 4));

                if(!error){
                    /* dprintf("Dome: state %d result %d\n", state, result); */

                    dome->dome_state = state;
                    dome->dome_result = result;
                } else {
                    /* dprintf("Dome: no connection\n"); */

                    dome->dome_state = -1;
                }

                processed += 2;
            }
            break;
        case 0xB8:
            if(length > 4 && check_crc(4)){
                int value = *((u_int8_t *)(data + 3));

                if(!error){
                    /* dprintf("Dehumidifier: channels %d dome %d ventillation in engineering %d - value %d\n", */
                    /*         value & 0x1, (value & 0x2) >> 1, (value & 0x4) >> 2, value); */

                    dome->dehum_channels = value & 0x1;
                    dome->dehum_dome = (value & 0x2) >> 1;
                    dome->vent_engineering = (value & 0x4) >> 2;
                } else {
                    /* dprintf("Dehumidifier: no connection\n"); */

                    dome->dehum_channels = -1;
                    dome->dehum_dome = -1;
                    dome->vent_engineering = -1;

                }

                processed += 1;
            }
            break;
        case 0xB4:
            if(length > 9 && check_crc(9)){
                int roof_conn = *((u_int8_t *)(data + 3));
                int roof_s0 = *((u_int8_t *)(data + 4));
                int roof_s1 = *((u_int8_t *)(data + 5));
                int endpoints = *((u_int8_t *)(data + 6));
                int sidewall = *((u_int8_t *)(data + 7));
                int handrail = *((u_int8_t *)(data + 8));

                /* dprintf("roof conn %d remote %d motion %d open %d close_near %d close %d\n", */
                /*         roof_conn, roof_s0 & 0x1, (roof_s0 & 0x6) >> 1, (roof_s0 & 0x8) >> 3, (roof_s0 & 0x10) >> 4, (roof_s0 & 0x20) >> 5); */
                /* dprintf("roof_state %d\n", roof_s1); */
                /* dprintf("endpoints %d\n", endpoints); */
                /* dprintf("sidewall %d\n", sidewall); */
                /* dprintf("handrail %d\n", handrail); */

                dome->sidewall = sidewall;
                dome->handrail = handrail;

                dome->roof_s0 = roof_s0;
                dome->roof_s1 = roof_s1;

                dome->remote = roof_s0 & 0x1;

                if((roof_s1 == 1 && (roof_s0 & 0x6) >> 1 == 1)  ||
                   dome->dome_state == 2 || dome->dome_state == 4 || dome->dome_state == 7 || dome->dome_state == 8 || dome->dome_state == 11 || dome->dome_state == 13)
                    dome->state = 1; /* Opening */
                else if((roof_s1 == 1 && (roof_s0 & 0x6) >> 1 == 2)   ||
                        dome->dome_state == 3 || dome->dome_state == 5 || dome->dome_state == 6 || dome->dome_state == 9 || dome->dome_state == 10 || dome->dome_state == 12 || dome->dome_state == 14)
                    dome->state = 2; /* Closing */
                else if(roof_s1 == 2)
                    dome->state = 3; /* Open */
                else if(roof_s1 == 3)
                    dome->state = 0; /* Close */
#if 0
                /* 'Edge cases' - error states where we can guess whether it is open or closed */
                else if((roof_s1 == 1 || roof_s1 == 0) && (roof_s0 & 0x8) == 0x8)
                    dome->state = 0;
                else if((roof_s1 == 1 || roof_s1 == 0) && (roof_s0 & 0x20) == 0x20)
                    dome->state = 0;
#endif
                /* Unknown state */
                else
                    dome->state = 4;

                processed += 6;
            }
            break;
        case 0xB0:
        case 0xB1:
        case 0xB2:
        case 0xB5:
        case 0xB6:
        case 0xB7:
            dprintf("Supported reply type %X, err %X\n", type, error);
            break;
        default:
            dprintf("Unsupported reply type %X, err %X, len %d\n", type, error, length);
            {
                char *string = make_long_hex_string(data, length);

                dprintf("%s\n", string);

                free(string);
            }
            break;
        }
    }

    if(processed && processed < length)
        processed += process_dome_data(server, conn, data + processed, length - processed, dome);

    return processed;
}

void dome_request_state_cb(server_str *server, int type, void *data)
{
    dome_str *dome = (dome_str *)data;

    if(dome->lowlevel_connection->is_connected){
        server_connection_write_block(dome->lowlevel_connection, "ENV", 3);
        server_connection_write_block(dome->lowlevel_connection, "PWI", 3);
        server_connection_write_block(dome->lowlevel_connection, "DBG", 3);
    }

    server_add_timer(server, 1.0, 0, dome_request_state_cb, dome);
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    dome_str *dome = (dome_str *)data;
    command_str *command = command_parse(string);

    /* server_default_line_read_hook(server, connection, string, data); */

    if(command_match(command, "exit") || command_match(command, "quit")){
        dome->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_dome_status")){
        server_connection_message(connection, "dome_status state=%d dome_state=%d dome_result=%d dehum_channels=%d dehum_dome=%d vent_engineering=%d sidewall=%d handrail=%d remote=%d roof_s0=%d roof_s1=%d",
                                  dome->state, dome->dome_state, dome->dome_result, dome->dehum_channels, dome->dehum_dome, dome->vent_engineering, dome->sidewall, dome->handrail, dome->remote, dome->roof_s0, dome->roof_s1);
    } else if(command_match(command, "open_dome")){
        server_connection_write_block(dome->lowlevel_connection, "OPN", 3);
    } else if(command_match(command, "close_dome")){
        server_connection_write_block(dome->lowlevel_connection, "CLS", 3);
    } else if(command_match(command, "stop_dome") || command_match(command, "stop")){
        server_connection_write_block(dome->lowlevel_connection, "STP", 3);
    } else if(command_match(command, "start_dehum") || command_match(command, "start_dehyd")){
        server_connection_write_block(dome->lowlevel_connection, "CDH\1", 4);
        server_connection_write_block(dome->lowlevel_connection, "DDH\1", 4);
        server_connection_write_block(dome->lowlevel_connection, "FLW\1", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "stop_dehum") || command_match(command, "stop_dehyd")){
        server_connection_write_block(dome->lowlevel_connection, "CDH\0", 4);
        server_connection_write_block(dome->lowlevel_connection, "DDH\0", 4);
        server_connection_write_block(dome->lowlevel_connection, "FLW\0", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "start_dehum_channels") || command_match(command, "start_dehyd_channels")){
        server_connection_write_block(dome->lowlevel_connection, "CDH\1", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "stop_dehum_channels") || command_match(command, "stop_dehyd_channels")){
        server_connection_write_block(dome->lowlevel_connection, "CDH\0", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "start_dehum_dome") || command_match(command, "start_dehyd_channels")){
        server_connection_write_block(dome->lowlevel_connection, "DDH\1", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "stop_dehum_dome") || command_match(command, "stop_dehyd_channels")){
        server_connection_write_block(dome->lowlevel_connection, "DDH\0", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "start_vent")){
        server_connection_write_block(dome->lowlevel_connection, "FLW\1", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "stop_vent")){
        server_connection_write_block(dome->lowlevel_connection, "FLW\0", 4);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

int main(int argc, char **argv)
{
    dome_str *dome = (dome_str *)malloc(sizeof(dome_str));
    int port = PORT_DOME;
    char *lowlevel_host = "192.168.16.11";
    int lowlevel_port = 20108;

    parse_args(argc, argv,
               "port=%d", &port,
               NULL);

    dome->server = server_create();

    dome->lowlevel_connection = server_add_connection(dome->server, lowlevel_host, lowlevel_port);
    dome->lowlevel_connection->is_active = TRUE;
    dome->lowlevel_connection->data_read_hook = process_dome_data;
    dome->lowlevel_connection->data_read_hook_data = dome;
    server_add_timer(dome->server, 0.1, 0, dome_request_state_cb, dome);

    SERVER_SET_HOOK(dome->server, line_read_hook, process_command, dome);

    dome->state = 0;
    dome->dome_state = 0;
    dome->dome_result = 0;
    dome->dehum_channels = 0;
    dome->dehum_dome = 0;
    dome->vent_engineering = 0;

    dome->sidewall = 0;
    dome->handrail = 0;

    dome->remote = 0;

    dome->is_quit = FALSE;

    server_listen(dome->server, "localhost", port);

    while(!dome->is_quit)
        server_cycle(dome->server, 10);

    server_delete(dome->server);

    return EXIT_SUCCESS;
}
