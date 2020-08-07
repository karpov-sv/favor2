#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#ifdef __linux__
#include <sys/io.h>
#endif

#include "utils.h"

#include "server.h"
#include "image_udp.h"
#include "command.h"

#include "ports.h"

static int MAX_VOLTAGE = 255;

static int is_connected = FALSE;

static int ii_power = 0;
static int ii_power_desired = -1;
static int focus = 0;
static int focus_desired = -1;
static int focus2 = 0;
static int focus2_desired = -1;

typedef struct hw_str {
    server_str *server;

    int is_quit;
} hw_str;

#define BASE_PORT 0xc010 /* was 378 - parport0, now parport1 */ /* printer port base address */
#define CONTROL_PORT (BASE_PORT + 2) /* control port */

enum parport_strobe_type {
    AUTO_LF,
    SEL_IN
};

enum parport_data_type {
    MOTORS,
    VOLTAGE
};

void parport_reset()
{
#ifdef __linux__
    if(!ioperm(BASE_PORT, 3, 1)){
        u_int8_t value = inb(CONTROL_PORT);

        value = value | 0x0a; /* set up 2 and 4 bits - C1 and C3 are off */

        outb(value, CONTROL_PORT);

        is_connected = TRUE;
    } else
        return_with_error("ioperm");
#endif
}

static void send_strobe_to_parport(enum parport_strobe_type type)
{
#ifdef __linux__
    if(is_connected){
        u_int8_t old_value = inb(CONTROL_PORT);
        u_int8_t new_value = old_value;

        switch(type){
        case AUTO_LF:
            new_value = new_value & 0xfd; /* C1, inversed 0010 */
            break;
        case SEL_IN:
            new_value = new_value & 0xf7; /* C3, inversed 1000 */
            break;
        }

        usleep(1000);
        /* send new value */
        outb(new_value, CONTROL_PORT);
        /* pause for 1 us */
        usleep(1000);
        /* restore original value */
        outb(old_value, CONTROL_PORT);
        usleep(1000);
    } else
        dprintf("Parport disconnected\n");
#endif
}

static void send_data_to_parport(enum parport_data_type type, u_int8_t value)
{
#ifdef __linux__
    if(is_connected){
        u_int8_t control = 0;

        switch(type){
        case MOTORS:
            control = 0x59; /* 01 011001 */
            break;
        case VOLTAGE:
            control = 0x99; /* 10 011001 */
            break;
        }

        outb(control, BASE_PORT);
        send_strobe_to_parport(AUTO_LF);
        outb(value, BASE_PORT);
        send_strobe_to_parport(SEL_IN);
    } else
        dprintf("Parport disconnected\n");
#endif
}

void parport_set_voltage(int d)
{
    u_int8_t value = 0xff - MIN(MAX(d, 0), 0xff);
    /* u_int8_t value = value & 0xff; */

    send_data_to_parport(VOLTAGE, value);
}

void parport_reset_motors()
{
    send_data_to_parport(MOTORS, 0);
}

void parport_move_motor(int motor, int dir, double duration)
{
    int value = 0;

    if(dir > 0)
        value = 1; /* 01 */
    else if(dir < 0)
        value = 2; /* 10 */
    else
        value = 0; /* 00 */
    if(motor == 0)
        value = value << 2; /* 0 - Main objective, 1 - transmission */

    send_data_to_parport(MOTORS, value);

    usleep(duration*1e6);

    send_data_to_parport(MOTORS, 0);
}

void parport_set_motor(int motor, int dir)
{
    int value = 0;

    if(dir > 0)
        value = 1; /* 01 */
    else if(dir < 0)
        value = 2; /* 10 */
    else
        value = 0; /* 00 */

    if(motor == 1)
        value = value << 2; /* Main objective, else - transmission */

    send_data_to_parport(MOTORS, value);
}

static void hw_callback(server_str *server, int type, void *data)
{
    connection_str *conn = (connection_str *)data;

    switch(type){
    case 1:
        server_connection_message(conn, "set_ii_power_done");
        ii_power = ii_power_desired;
        /* ii_power_desired = -1; */
        break;
    case 2:
        server_connection_message(conn, "set_motor_done");
        break;
    case 3:
        server_connection_message(conn, "move_focus_done");
        focus = focus_desired;
        /* focus_desired = -1; */
        break;
    case 4:
        server_connection_message(conn, "reset_done");
        break;
    case 5:
        server_connection_message(conn, "reset_focus_done");
        focus = focus_desired;
        /* focus_desired = -1; */
        /* focus2_desired = -1; */
        break;
    case 6:
        server_connection_message(conn, "move_focus_done");
        focus2 = focus2_desired;
        /* focus2_desired = -1; */
        break;
    }
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    hw_str *hw = (hw_str *)data;
    command_str *command = command_parse(string);

    server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        hw->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "set_ii_power")){
        int value = 1;

        command_args(command, "value=%d", &value, NULL);

        parport_set_voltage(value ? MAX_VOLTAGE : 0);
        ii_power_desired = value;
        server_add_timer(server, 5.0, 1, hw_callback, connection);
    } else if(command_match(command, "set_max_voltage")){
        int value = 255;

        command_args(command, "value=%d", &value, NULL);

        MAX_VOLTAGE = MAX(0, MIN(value, 255));

        parport_set_voltage(ii_power ? MAX_VOLTAGE : 0);
        server_connection_message(connection, "set_max_voltage_done");
    } else if(command_match(command, "set_motor")){
        int motor = 0;
        int direction = 0;

        command_args(command,
                     "motor=%d", &motor,
                     "direction=%d", &direction,
                     NULL);
        parport_set_motor(motor, direction);

        server_add_timer(server, 1.0, 2, hw_callback, connection);
    } else if(command_match(command, "move_focus")){
        int id = 0;
        int shift = 0;
        int direction = 1;
        double duration = 1.0;

        command_args(command,
                     "shift=%d", &shift,
                     "id=%d", &id,
                     NULL);

        duration = 0.1*fabs(shift);
        direction = sign(shift);

        parport_move_motor(id, direction, duration);

        if(id == 0){
            focus_desired = focus + shift;
            server_add_timer(server, 5.0, 3, hw_callback, connection);
        } else {
            focus2_desired = focus2 + shift;
            server_add_timer(server, 5.0, 6, hw_callback, connection);
        }

    } else if(command_match(command, "reset")){
        parport_reset();
        server_add_timer(server, 1.0, 4, hw_callback, connection);
    } else if(command_match(command, "reset_focus")){
        parport_reset_motors();

        server_add_timer(server, 1.0, 4, hw_callback, connection);
    } else if(command_match(command, "get_hw_status")){
        server_connection_message(connection, "hw_status connected=%d focus=%d focus2=%d ii_power=%d max_voltage=%d",
                                  is_connected, focus, focus2, ii_power, MAX_VOLTAGE);
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

static void connection_disconnected(server_str *server, connection_str *conn, void *data)
{
    timer_str *timer;

    /* Kill the timers linked to this connection */
    foreach(timer, server->timers){
        if(timer->callback_data == conn){
            del_from_list_in_foreach_and_run(timer, free(timer));
        }
    }
}

static void motors_reset_timer(server_str *server, int type, void *data)
{
    hw_str *hw = (hw_str *)data;

    dprintf("Resetting motors\n");

    parport_reset_motors();

    server_add_timer(hw->server, 1.0, 0, motors_reset_timer, hw);
}

int main(int argc, char **argv)
{
    hw_str *hw = (hw_str *)malloc(sizeof(hw_str));
    int port = PORT_CHANNEL_HW;

    parse_args(argc, argv,
               "port=%d", &port,
               NULL);

    parport_reset();
    parport_set_voltage(0);
    parport_reset_motors();

    hw->server = server_create();
    hw->is_quit = FALSE;

    SERVER_SET_HOOK(hw->server, line_read_hook, process_command, hw);
    SERVER_SET_HOOK(hw->server, connection_disconnected_hook, connection_disconnected, hw);

    server_listen(hw->server, "localhost", port);

    motors_reset_timer(hw->server, 0, hw);

    while(!hw->is_quit){
        server_cycle(hw->server, 0.1);
    }

    server_delete(hw->server);
    free(hw);

    return EXIT_SUCCESS;
}
