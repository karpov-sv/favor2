#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "image_udp.h"
#include "command.h"

#include "ports.h"

typedef struct hw_str {
    server_str *server;
    connection_str *grabber_connection;

    int focus;
    int focus_desired;
    int focus_speed;

    int focus2;
    int focus2_desired;
    int focus2_speed;

    int ii_power;

    int cover;

    int lamp;

    int camera;

    int filters;

    int mirror1;
    int mirror2;
} hw_str;

void set_focus(hw_str *hw, int value)
{
    double fwhm = 0;

    hw->focus = MIN(4095, MAX(0, value));

    /* fwhm = 10.0*(1 - 1.0/(pow(hw->focus - 3620, 2)/3000 + 1.03)); */
    fwhm = 10.0*(1 - 1.0/(pow(hw->focus - 3620, 2)/30000 + 1.09)) + 1.0*pow(hw->focus2-100, 2)/20000;

    dprintf("Focus set to %d\n", value);

    server_connection_message(hw->grabber_connection, "set_fwhm %g", fwhm);
}

void set_focus_cb(server_str *server, int type, void *ptr)
{
    hw_str *hw = (hw_str *)ptr;
    int delta = hw->focus_desired - hw->focus;

    if(abs(delta) > hw->focus_speed)
        delta = sign(delta)*hw->focus_speed;

    set_focus(hw, hw->focus + delta);

    if(hw->focus != hw->focus_desired)
        server_add_timer(hw->server, 0.01, 0, set_focus_cb, hw);
}

void set_focus_slow(hw_str *hw, int value)
{
    hw->focus_desired = value;

    server_add_timer(hw->server, 0.01, 0, set_focus_cb, hw);
}

void set_focus2(hw_str *hw, int value)
{
    double fwhm = 0;

    hw->focus2 = MIN(300, MAX(0, value));

    /* fwhm = 10.0*(1 - 1.0/(pow(hw->focus2 - 3620, 2)/3000 + 1.03)); */
    fwhm = 10.0*(1 - 1.0/(pow(hw->focus - 3620, 2)/30000 + 1.09)) + 1.0*pow(hw->focus2-100, 2)/20000;

    dprintf("Focus2 set to %d\n", value);

    server_connection_message(hw->grabber_connection, "set_fwhm %g", fwhm);
}

void set_focus2_cb(server_str *server, int type, void *ptr)
{
    hw_str *hw = (hw_str *)ptr;
    int delta = hw->focus2_desired - hw->focus2;

    if(abs(delta) > hw->focus2_speed)
        delta = sign(delta)*hw->focus2_speed;

    set_focus2(hw, hw->focus2 + delta);

    if(hw->focus2 != hw->focus2_desired)
        server_add_timer(hw->server, 0.01, 0, set_focus2_cb, hw);
}

void set_focus2_slow(hw_str *hw, int value)
{
    hw->focus2_desired = value;

    server_add_timer(hw->server, 0.01, 0, set_focus2_cb, hw);
}

void set_ii_power(hw_str *hw, int value)
{
    hw->ii_power = value ? 1 : 0;

    server_connection_message(hw->grabber_connection, "set_amplify %d", hw->ii_power);
}

void set_celostate(hw_str *hw, int pos1, int pos2)
{
    hw->mirror1 = pos1;
    hw->mirror2 = pos2;

    server_connection_message(hw->grabber_connection, "set_relative %g %g", 9.0*pos1/128+1.0*pos2/128, -1.0*pos1/128+9.0*pos2/128);
}

int process_hw_data(server_str *server, connection_str *conn, char *data, int length, void *ptr)
{
    hw_str *hw = (hw_str *)ptr;
    int processed = 0;
    char *string = make_long_hex_string(data, length);
    unsigned char reply[6];

    *(reply) = '*';
    *(reply + 1) = 0x0;
    *(reply + 2) = 0x0;

    /* dprintf("\nHW data: %d bytes\n", length); */
    /* dprintf("%s\n", string); */

    while(length - processed >= 6){
        char *command = strndup(data + processed, 5);
        int reply_type = 0x0;
        int reply_length = 4;

        /* if(*(data + processed + 2) != '_' && *(data + processed + 3) != '_'){ */
        /*     dprintf("wrong command! '%s'\n", command); */
        /*     //exit(1); */
        /* } */

        //dprintf("command: '%s'\n", command);

        processed += 5;

        if(!strcmp(command, "CNCFQ") && length - processed >= 1){
            int value = *(int8_t *)(data + processed);
            processed += 1;
            set_focus_slow(hw, value ? 4095 : 0);
            dprintf("resetting the focus\n");
            reply_type = 0x11;
        } else if(!strcmp(command, "CNCFS")){
            int value = *(int16_t *)(data + processed);
            processed += 2;
            set_focus_slow(hw, hw->focus + value);
            dprintf("moving the focus %d steps, now is %d\n", value, hw->focus);
            reply_type = 0x12;
        } else if(!strcmp(command, "CNGFS")){
            reply_type = 0x14;
            *(int16_t *)(reply + 3) = hw->focus;
            reply_length += 2;
        } else if(!strcmp(command, "TLZPS")){
            set_focus2_slow(hw, 0);
            dprintf("resetting the focus2\n");
            reply_type = 0x3A;
        } else if(!strcmp(command, "TLREL")){
            int value = *(int16_t *)(data + processed);
            processed += 2;
            set_focus2_slow(hw, hw->focus2 + value);
            dprintf("moving the focus2 %d steps, now is %d\n", value, hw->focus2);
            reply_type = 0x31;
        } else if(!strcmp(command, "TLGET")){
            reply_type = 0x32;
            *(int16_t *)(reply + 3) = hw->focus2;
            reply_length += 2;
        } else if(!strcmp(command, "CNCAS") && length - processed >= 1){
            reply_type = 0x13;
            processed += 1;
        } else if(!strcmp(command, "IIPWR") && length - processed >= 1){
            int value = *(int8_t *)(data + processed);
            reply_type = 0x20;
            processed += 1;
            set_ii_power(hw, value);
        } else if(!strcmp(command, "IIGET")){
            reply_type = 0x21;
            *(int8_t *)(reply + 3) = hw->ii_power;
            reply_length += 1;
        } else if(!strcmp(command, "LMSET") && length - processed >= 1){
            int value = *(int8_t *)(data + processed);
            processed += 1;
            hw->lamp = value;
        } else if(!strcmp(command, "LMGET")){
            reply_type = 0x66;
            *(int8_t *)(reply + 3) = hw->lamp;
            reply_length += 1;
        } else if(!strcmp(command, "CVSET") && length - processed >= 1){
            int value = *(int8_t *)(data + processed);
            reply_type = 0x20;
            processed += 1;
            hw->cover = value;
        } else if(!strcmp(command, "CVGET")){
            reply_type = 0x61;
            *(int8_t *)(reply + 3) = hw->cover;
            *(int8_t *)(reply + 4) = hw->cover ? 0x1 : 0x2;
            reply_length += 2;
        } else if(!strcmp(command, "FTSET") && length - processed >= 1){
            int value = *(int8_t *)(data + processed);
            reply_type = 0x20;
            processed += 1;
            hw->filters = value;
        } else if(!strcmp(command, "FTGET")){
            int sensors = 0;

            sensors += hw->filters & 0x01 ? 0x01 : 0x02;
            sensors += hw->filters & 0x02 ? 0x04 : 0x08;
            sensors += hw->filters & 0x04 ? 0x10 : 0x20;
            sensors += hw->filters & 0x08 ? 0x40 : 0x80;


            reply_type = 0x41;
            *(int8_t *)(reply + 3) = hw->filters;
            *(int8_t *)(reply + 4) = sensors;
            reply_length += 2;
        } else if(!strcmp(command, "MRSET") && length - processed >= 2){
            int value1 = *(int8_t *)(data + processed);
            int value2 = *(int8_t *)(data + processed + 1);
            reply_type = 0x50;
            processed += 2;
            set_celostate(hw, value1, value2);
        } else if(!strcmp(command, "MRGET")){
            reply_type = 0x51;

            *(int8_t *)(reply + 3) = hw->mirror1;
            *(int8_t *)(reply + 4) = hw->mirror2;
            *(int16_t *)(reply + 5) = hw->mirror1;
            *(int16_t *)(reply + 7) = hw->mirror2;

            reply_length += 6;
        } else if(!strcmp(command, "THGTM")){
            reply_type = 0x72;

            *(int16_t *)(reply + 3) = 12.00;
            *(int16_t *)(reply + 5) = 24.00;
            *(int16_t *)(reply + 7) = 48.00;

            reply_length += 6;
        } else if(!strcmp(command, "THGTS")){
            reply_type = 0x73;

            *(int16_t *)(reply + 3) = 12.00;
            *(int16_t *)(reply + 5) = 24.00;
            *(int16_t *)(reply + 7) = 48.00;

            reply_length += 6;
        } else if(!strcmp(command, "CPPWR") && length - processed >= 1){
            int value = *(int8_t *)(data + processed);
            processed += 1;
            hw->camera = value;
        } else if(!strcmp(command, "CPGET")){
            reply_type = 0x81;
            *(int8_t *)(reply + 3) = hw->camera;
            reply_length += 1;
        } else {
            dprintf("Unhandled command %s\n", command);
            goto END;
        }
        *(reply + 1) = reply_type;
        *(reply + reply_length - 1) = crc8(reply, reply_length - 1);
        server_connection_write_block(conn, (char *)reply, reply_length);
    END:
        free(command);
    }

    return processed;
}

void client_connected_hook(server_str *server, connection_str *connection, void *data)
{
    connection->data_read_hook = process_hw_data;
    connection->data_read_hook_data = data;

    server_default_client_connected_hook(server, connection, data);
}

int main(int argc, char **argv)
{
    hw_str *hw = (hw_str *)malloc(sizeof(hw_str));
    char *host = "localhost";
    int port = PORT_CHANNEL_LOWLEVEL;

    parse_args(argc, argv,
               "host=%s", &host,
               "port=%d", &port,
               NULL);

    hw->server = server_create();

    SERVER_SET_HOOK(hw->server, client_connected_hook, client_connected_hook, hw);

    hw->grabber_connection = server_add_connection(hw->server, "localhost", PORT_GRABBER_CONTROL);
    hw->grabber_connection->is_active = TRUE;
    hw->focus = 0;
    hw->focus_desired = 0;
    hw->focus_speed = 10;

    hw->focus2 = 0;
    hw->focus2_desired = 0;
    hw->focus2_speed = 1;

    hw->ii_power = 0;

    hw->cover = 0;
    hw->lamp = 0;
    hw->camera = 0;
    hw->filters = 0;

    hw->mirror1 = 0;
    hw->mirror2 = 0;

    server_listen(hw->server, host, port);

    while(1){
        server_cycle(hw->server, 1);
    }

    server_delete(hw->server);
    free(hw);
}
