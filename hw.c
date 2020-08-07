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

static time_str focus_reset_timestamp;
static int focus_current = -1;
static int focus_desired = -1;
static int focus_zero = 0;
static int focus2_current = -1;
static int focus2_desired = -1;
static int ii_current = -1;
static int ii_desired = -1;
static time_str ii_timestamp;
static int cover_current = -1;
static int cover_desired = -1;
static int filters_current = -1;
static int filters_desired = -1;
static int filters_state[4] = {-1, -1, -1 , -1};

/* Focus values for different filters, relative to focus_zero */
static int focus_pos_Clear = -1;
static int focus_pos_B = -1;
static int focus_pos_V = -1;
static int focus_pos_R = -1;

static int lamp_desired = -1;
static int lamp_current = -1;

static int camera_desired = -1;
static int camera_current = -1;

static int celostate_pos0 = -1;
static int celostate_pos1 = -1;
static int celostate_real0 = -1;
static int celostate_real1 = -1;
static int celostate_moving = FALSE;
static time_str celostate_timestamp;

static double camera_temperature = -1;
static double camera_humidity = -1;
static double camera_dewpoint = -1;
static double celostate_temperature = -1;
static double celostate_humidity = -1;
static double celostate_dewpoint = -1;

static time_str last_status_time;
static time_str last_error_time;

typedef struct hw_str {
    server_str *server;

    connection_str *lowlevel_connection;
    connection_str *logger_connection;

    command_queue_str *commands;

    int id;

    int is_quit;
} hw_str;

void hw_log(hw_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));

void hw_log(hw_str *hw, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("%s: Log: %s\n", timestamp(), buffer);

    if(hw->logger_connection)// && hw->logger_connection->is_connected)
        server_connection_message(hw->logger_connection, buffer);

    free(buffer);
}

void store_focus_pos(hw_str *hw)
{
    char *filename = "hw.focus";
    FILE *file = fopen(filename, "w");

    if(file){
        fprintf(file, "Clear %d B %d V %d R %d\n", focus_pos_Clear, focus_pos_B, focus_pos_V, focus_pos_R);

        dprintf("%s: Focus calibration for filters stored to %s\n", timestamp(), filename);

        fclose(file);
    }
}

void restore_focus_pos(hw_str *hw)
{
    char *filename = "hw.focus";
    FILE *file = fopen(filename, "r");

    if(file){
        if(fscanf(file, "Clear %d B %d V %d R %d\n", &focus_pos_Clear, &focus_pos_B, &focus_pos_V, &focus_pos_R) == 4)
            dprintf("%s: Focus calibration for filters restored from %s\n", timestamp(), filename);

        fclose(file);
    }
}

void hw_focus_reset(hw_str *hw, int id)
{
    if(id == 0){
        focus_desired = -10000;
        focus_reset_timestamp = time_current();
        focus_zero = 9999;
        server_connection_write_block(hw->lowlevel_connection, "CNCFQ\0", 6);
    } else {
        focus2_desired = 0;
        server_connection_write_block(hw->lowlevel_connection, "TLZPS\0", 6);
    }
}

void hw_focus_move(hw_str *hw, int id, int shift)
{
    /* We shall not send commands with arguments while controller is booting */
    if(1e-3*time_interval(last_error_time, time_current()) < 5.0)
        return;

    if(id == 0){
        char str[7] = "CNCFS  ";

        *(int16_t *)(str + 5) = shift;

        focus_desired = focus_current + shift;

        dprintf("%s: Moving focus: current=%d shift=%d desired=%d zero=%d\n",
                timestamp(), focus_current, shift, focus_desired, focus_zero);
        server_connection_write_block(hw->lowlevel_connection, str, 7);
    } else {
        char str[7] = "TLREL  ";

        *(int16_t *)(str + 5) = shift;

        focus2_desired = focus2_current + shift;
        server_connection_write_block(hw->lowlevel_connection, str, 7);
    }
}

void hw_aperture_move(hw_str *hw, int shift)
{
    char str[7] = "CNCAS\0";

    /* We shall not send commands with arguments while controller is booting */
    if(1e-3*time_interval(last_error_time, time_current()) < 5.0)
        return;

    *(int8_t *)(str + 5) = shift;

    server_connection_write_block(hw->lowlevel_connection, str, 6);
}

void hw_ii_power(hw_str *hw, int value)
{
    char str[6] = "IIPWR\0";

    value = value ? 1 : 0;
    *(str + 5) = value;

    ii_desired = value;
    ii_timestamp = time_current();
    server_connection_write_block(hw->lowlevel_connection, str, 6);

    hw_log(hw, "II power: turning %s", value ? "on" : "off");
}

void hw_cover_set(hw_str *hw, int value)
{
    char str[6] = "CVSET\0";

    value = value ? 1 : 0;
    *(str + 5) = value;

    cover_desired = value;
    server_connection_write_block(hw->lowlevel_connection, str, 6);

    hw_log(hw, "Cover: %s", value ? "opening" : "closing");
}

void hw_cover_stop(hw_str *hw)
{
    char str[5] = "CVSTP";

    server_connection_write_block(hw->lowlevel_connection, str, 5);

    hw_log(hw, "Cover: stopping");
}

void hw_lamp_power(hw_str *hw, int value)
{
    char str[6] = "LMSET\0";

    value = value ? 1 : 0;
    *(str + 5) = value;

    lamp_desired = value;
    server_connection_write_block(hw->lowlevel_connection, str, 6);

    hw_log(hw, "Flatfield lamp: turning %s", value ? "on" : "off");
}

void hw_camera_power(hw_str *hw, int value)
{
    char str[6] = "CPPWR\0";

    value = value ? 1 : 0;
    *(str + 5) = value;

    camera_desired = value;
    server_connection_write_block(hw->lowlevel_connection, str, 6);

    hw_log(hw, "Camera power: turning %s", value ? "on" : "off");
}

void hw_filters_set(hw_str *hw, int value)
{
    char str[6] = "FTSET\0";

    /* We shall not send commands with arguments while controller is booting */
    if(1e-3*time_interval(last_error_time, time_current()) < 5.0)
        return;

    value = value & 0x0F;
    *(str + 5) = value;

    filters_desired = value;
    server_connection_write_block(hw->lowlevel_connection, str, 6);

    hw_log(hw, "Filters: setting to %d", value);
}

void hw_filters_calibrate(hw_str *hw)
{
    char str[5] = "FTCBR";

    server_connection_write_block(hw->lowlevel_connection, str, 5);
}

void hw_mirror_set(hw_str *hw, int pos0, int pos1)
{
    char str[7] = "MRSET\0\0";

    /* We shall not send commands with arguments while controller is booting */
    if(1e-3*time_interval(last_error_time, time_current()) < 5.0)
        return;

    /* Allowed positions are in -127..127 range */
    pos0 = MAX(-127, MIN(127, pos0));
    pos1 = MAX(-127, MIN(127, pos1));

    *(int8_t *)(str + 5) = pos0;
    *(int8_t *)(str + 6) = pos1;

    celostate_moving = TRUE;
    celostate_timestamp = time_current();
    server_connection_write_block(hw->lowlevel_connection, str, 7);

    hw_log(hw, "Celostate: setting to %d %d", pos0, pos1);
}

void hw_restart(hw_str *hw)
{
    char str[3] = "RST";

    server_connection_write_block(hw->lowlevel_connection, str, 3);

    hw_log(hw, "HW: restarting controller");
}

void hw_request_state_cb(server_str *server, int type, void *data)
{
    hw_str *hw = (hw_str *)data;
    static int state_counter = 0;

    if(hw->lowlevel_connection->is_connected){
        switch(state_counter){
        case 0:
            server_connection_write_block(hw->lowlevel_connection, "CNGFS", 5);
            break;
        case 1:
            server_connection_write_block(hw->lowlevel_connection, "TLGET", 5);
            break;
        case 2:
            server_connection_write_block(hw->lowlevel_connection, "IIGET", 5);
            break;
        case 3:
            server_connection_write_block(hw->lowlevel_connection, "CVGET", 5);
            break;
        case 4:
            server_connection_write_block(hw->lowlevel_connection, "FTGET", 5);
            break;
        case 5:
            server_connection_write_block(hw->lowlevel_connection, "MRGET", 5);
            break;
        case 6:
            server_connection_write_block(hw->lowlevel_connection, "THGTM", 5);
            break;
        case 7:
            server_connection_write_block(hw->lowlevel_connection, "THGTS", 5);
            break;
        case 8:
            server_connection_write_block(hw->lowlevel_connection, "LMGET", 5);
            break;
        case 9:
            server_connection_write_block(hw->lowlevel_connection, "CPGET", 5);
        default:
            break;
        }

        state_counter ++;

        if(state_counter > 10)
            state_counter = 0;
    }

    server_add_timer(server, 0.03, 0, hw_request_state_cb, hw);
}

void hw_keep_ii_cb(server_str *server, int type, void *data)
{
    hw_str *hw = (hw_str *)data;

    if(hw->lowlevel_connection->is_connected){
        if(ii_current == 1 && ii_desired != 0)
            server_connection_write_block(hw->lowlevel_connection, "IIPWR\1", 6);
    }

    server_add_timer(server, 0.1, 0, hw_keep_ii_cb, hw);
}

void hw_keep_camera_cb(server_str *server, int type, void *data)
{
    hw_str *hw = (hw_str *)data;

    if(hw->lowlevel_connection->is_connected){
        if(camera_current == 1 && camera_desired != 0)
            server_connection_write_block(hw->lowlevel_connection, "CPPWR\1", 6);
    }

    server_add_timer(server, 0.1, 0, hw_keep_camera_cb, hw);
}

int hw_focus_get(hw_str *hw, int id)
{
    return (id == 0) ? focus_current : focus2_current;
}

void update_hw_state(hw_str *hw)
{
    if(focus_desired == -10000 && 1e-3*time_interval(focus_reset_timestamp, time_current()) > 5){
        /* Update zero focus position, as we are in focus resetting state */
        dprintf("%s: New zero focus is %d, was %d\n", timestamp(), focus_current, focus_zero);
        focus_zero = focus_current;

        focus_desired = -1;
        command_queue_done(hw->commands, "reset_focus");
    }
    if(focus_current != -1 && (focus_current == focus_desired ||
                               (focus_current == focus_zero + 1 && focus_desired == focus_zero))){
        focus_desired = -1;
        if(!command_queue_done(hw->commands, "reset_focus"))
            if(!command_queue_done(hw->commands, "move_focus"))
                command_queue_done(hw->commands, "set_focus");
    }
    if(focus2_current != -1 && focus2_current == focus2_desired){
        focus2_desired = -1;
        if(!command_queue_done(hw->commands, "reset_focus"))
            command_queue_done(hw->commands, "move_focus");
    }
    if(ii_current != -1 && ii_current == ii_desired && 1e-3*time_interval(ii_timestamp, time_current()) > 5){
        ii_desired = -1;
        command_queue_done(hw->commands, "set_ii_power");
        hw_log(hw, "II power: %s", ii_current ? "on" : "off");
    }
    if(lamp_current != -1 && lamp_current == lamp_desired){
        lamp_desired = -1;
        command_queue_done(hw->commands, "set_lamp");
        hw_log(hw, "Flatfield lamp: %s", lamp_current ? "on" : "off");
    }
    if(camera_current != -1 && camera_current == camera_desired){
        camera_desired = -1;
        command_queue_done(hw->commands, "set_camera");
        hw_log(hw, "Camera power: %s", camera_current ? "on" : "off");
    }
    if(cover_current != -1 && cover_current == cover_desired){
        cover_desired = -1;
        command_queue_done(hw->commands, "set_cover");
        hw_log(hw, "Cover: %s", cover_current ? "opened" : "closed");
    }
    if(cover_current == 2){
        cover_current = -1;
        cover_desired = -1;
        command_queue_done(hw->commands, "stop_cover");
        hw_log(hw, "Cover: stopped");
    }
    if(filters_current != -1 && filters_current == filters_desired){
        /* FIXME: here we really need to check whether focus is already properly set */
        filters_desired = -1;
        command_queue_done(hw->commands, "set_filters");
        hw_log(hw, "Filters: set to %d", filters_current);
    }
    /* FIXME: find better way to handle celostate moving completion */
    if(celostate_moving && 1e-3*time_interval(celostate_timestamp, time_current()) > 1.0){
        celostate_moving = FALSE;
        command_queue_done(hw->commands, "set_mirror");
        hw_log(hw, "Celostate: set");
    }
}

int process_hw_data(server_str *server, connection_str *conn, char *data, int length, void *ptr)
{
    hw_str *hw = (hw_str *)ptr;
    int processed = 0;
    int error = 0;

    int check_crc(int len)
    {
        return crc8((unsigned char *)data, len) == *(u_int8_t*)(data + len) && !error;
    }

    if(!length)
        return processed;

    if(*data == '$'){
        dprintf("\n\n\n%s: HW controller is in wrong regime!\n\n\n", timestamp());
        last_error_time = time_current();
        return 1;
    }

    if(*data != '*'){
        dprintf("%s: Unknown reply char encountered! %d\n", timestamp(), (int)(*data));
        {
            char *string = make_long_hex_string(data, length);

            dprintf("%s\n", string);

            free(string);
        }
        processed = 1;
    } else if(length > 3){
        int type = *((u_int8_t *)(data + 1));

        error = *((u_int8_t *)(data + 2));

        /* dprintf("reply %X, length %d, error %d\n", type, length, error); */
        processed = 4;

        switch(type){
        case 0x10: /* CN_CFQ */
        case 0x11: /* CN_CFM */
        case 0x12: /* CN_CFS */
        case 0x13: /* CN_CAS */
        case 0x20:
        case 0x40:
        case 0x50:
        case 0x80: /* CPPWR */
            break;
        case 0x14: /* CN_GFS */
            if(length > 5 && check_crc(5)){
                int value = *((int16_t *)(data + 3));

                if(value != 0x7fff){
                    focus_current = value;
                    update_hw_state(hw);
                }

                processed += 2;

                last_status_time = time_current();
            }
            break;
        case 0x21: /* II_GET */
            if(length > 4 && check_crc(4)){
                int value = *((u_int8_t *)(data + 3));

                ii_current = value;
                update_hw_state(hw);
                processed += 1;
            }
            break;
        case 0x32: /* TL_GET */
            if(length > 5 && check_crc(5)){
                int value = *((int16_t *)(data + 3));

                focus2_current = value;
                update_hw_state(hw);
                processed += 2;
            }
            break;
        case 0x41: /* FT_GET */
            if(length > 5 && check_crc(5)){
                //int info = *((u_int8_t *)(data + 3));
                int sensors = *((u_int8_t *)(data + 4));
                int count = 0;
                int i;

                /* Parse sensors */
                for(i = 0; i < 4; i++){
                    filters_state[i] = -1;

                    if(sensors & 1 << (2*i)){
                        filters_state[i] = 1;
                        count ++;
                    } else if(sensors & 1 << (2*i + 1)){
                        filters_state[i] = 0;
                        count ++;
                    } else if(sensors & 3 << (2*i)){
                        filters_state[i] = -2;
                    }
                }

                if(count == 4){
                    filters_current = 0;

                    for(i = 0; i < 4; i++){
                        if(filters_state[i] < 0){
                            filters_current = -1;
                            break;
                        }

                        filters_current += filters_state[i]*(1 << i);
                    }
                    update_hw_state(hw);
                }

                //dprintf("info = %s, sensors = %s\n", make_binary_string(info), make_binary_string(sensors));
                // update_hw_state(hw);
                processed += 2;
            }
            break;
        case 0x51: /* MR_GET */
            if(length > 9 && check_crc(9)){
                int pos0 = *((int8_t *)(data + 3));
                int pos1 = *((int8_t *)(data + 4));
                int real0 = *((int16_t *)(data + 5));
                int real1 = *((int16_t *)(data + 7));

                celostate_pos0 = pos0;
                celostate_pos1 = pos1;
                celostate_real0 = real0;
                celostate_real1 = real1;

                //dprintf("%d: %d (%d) - %d (%d)\n", length, real0, pos0, real1, pos1);

                processed += 6;
            }
            break;
        case 0x61: /* CV_GET */
            if(length > 5 && check_crc(5)){
                /* int target_status = *((u_int8_t *)(data + 3)); */
                int sensors = *((u_int8_t *)(data + 4));

                if(sensors == 1)
                    cover_current = 1;
                else if(sensors == 2)
                    cover_current = 0;
                else
                    cover_current = -1;
                update_hw_state(hw);
                processed += 2;
            }
            break;
        case 0x62: /* CV_STOP */
            cover_current = 2;
            update_hw_state(hw);
            break;
        case 0x66: /* LAMP_GET */
            if(length > 4 && check_crc(4)){
                int value = *((u_int8_t *)(data + 3));

                lamp_current = value ? 1 : 0;
                update_hw_state(hw);
                processed += 1;
            }
            break;
        case 0x72: /* TH_GETM */
            if(length > 9 && check_crc(9)){
                camera_temperature = *((int16_t *)(data + 3)) * 0.01;
                camera_humidity = *((u_int16_t *)(data + 5)) * 0.01;
                camera_dewpoint = *((int16_t *)(data + 7)) * 0.01;

                processed += 6;
            }
            break;
        case 0x73: /* TH_GETS */
            if(length > 9 && check_crc(9)){
                celostate_temperature = *((int16_t *)(data + 3)) * 0.01;
                celostate_humidity = *((u_int16_t *)(data + 5)) * 0.01;
                celostate_dewpoint = *((int16_t *)(data + 7)) * 0.01;

                processed += 6;
            }
            break;
        case 0x81: /* CP_GET */
            if(length > 4 && check_crc(4)){
                int value = *((u_int8_t *)(data + 3));

                camera_current = value;
                update_hw_state(hw);
                processed += 1;
            }
            break;
        default:
            dprintf("%s: Unsupported reply type %X, err %X\n", timestamp(), type, error);
            {
                char *string = make_long_hex_string(data, length);

                dprintf("%s\n", string);

                free(string);
            }
            break;
        }
    }

    return processed;
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    hw_str *hw = (hw_str *)data;
    command_str *command = command_parse(string);

    //server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        hw->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "restart")){
        hw_restart(hw);
        server_connection_message(connection, "restart_ok");
    } else if(command_match(command, "set_ii_power")){
        int value = 1;

        command_args(command, "value=%d", &value, NULL);

        if(ii_current != value){
            hw_ii_power(hw, value);
            command_queue_add(hw->commands, connection, command_name(command), 10);
        } else
            server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_lamp")){
        int value = 1;

        command_args(command, "value=%d", &value, NULL);

        hw_lamp_power(hw, value);
        command_queue_add(hw->commands, connection, command_name(command), 10);
    } else if(command_match(command, "set_camera")){
        int value = 1;

        command_args(command, "value=%d", &value, NULL);

        hw_camera_power(hw, value);
        command_queue_add(hw->commands, connection, command_name(command), 10);
    } else if(command_match(command, "reset_focus")){
        int id = 0;

        command_args(command,
                     "id=%d", &id,
                     NULL);
        hw_focus_reset(hw, id);
        command_queue_add(hw->commands, connection, command_name(command), 10);
    } else if(command_match(command, "move_focus")){
        int id = 0;
        int shift = 100;

        command_args(command,
                     "shift=%d", &shift,
                     "id=%d", &id,
                     NULL);

        if(shift){
            hw_focus_move(hw, id, shift);
            command_queue_add(hw->commands, connection, command_name(command), 10);
        } else
            server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_focus")){
        int id = 0;
        int pos = 0;
        int shift = 0;

        command_args(command,
                     "pos=%d", &pos,
                     "id=%d", &id,
                     NULL);

        /* FIXME: this command works only after zero point calibration by reset_focus()! */
        if(id == 0)
            shift = pos - (focus_current - focus_zero);
        else
            shift = pos - focus2_current;

        if(shift){
            hw_focus_move(hw, id, shift);
            command_queue_add(hw->commands, connection, command_name(command), 10);
        } else
            server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "remember_focus")){
        int fast = FALSE;
        int new_Clear = focus_pos_Clear;
        int new_B = focus_pos_B;
        int new_V = focus_pos_V;
        int new_R = focus_pos_R;

        command_args(command,
                     "clear=%d", &new_Clear,
                     "b=%d", &new_B,
                     "v=%d", &new_V,
                     "r=%d", &new_R,
                     "fast=%d", &fast,
                     NULL);

        if(fast){
            int delta = new_Clear - focus_pos_Clear;

            new_B += delta;
            new_V += delta;
            new_R += delta;
        }

        focus_pos_Clear = new_Clear;
        focus_pos_B = new_B;
        focus_pos_V = new_V;
        focus_pos_R = new_R;

        if(focus_pos_Clear >= 0 &&
           focus_pos_B >= 0 &&
           focus_pos_V >= 0 &&
           focus_pos_R >= 0)
            store_focus_pos(hw);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "move_aperture") || command_match(command, "aperture_move")){
        int shift = 0;

        command_args(command,
                     "shift=%d", &shift,
                     NULL);

        hw_aperture_move(hw, shift);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_cover")){
        int value = 1;

        command_args(command, "state=%d", &value, NULL);

        hw_cover_set(hw, value);
        command_queue_add(hw->commands, connection, command_name(command), 10);
    } else if(command_match(command, "stop_cover")){
        hw_cover_stop(hw);
        command_queue_add(hw->commands, connection, command_name(command), 1);
    } else if(command_match(command, "set_filters")){
        int value = 0;
        char *name = NULL;
        int focus_pos = focus_pos_Clear;

        command_args(command,
                     "filters=%d", &value,
                     "name=%s", &name,
                     NULL);

        /* TODO: check filter numbers! */
        if(name){
            if(!strcmp(name, "Clear")){
                value = 0;
                focus_pos = focus_pos_Clear;
            } else if(!strcmp(name, "B")){
                value = 1;
                focus_pos = focus_pos_B;
            } else if(!strcmp(name, "V")){
                value = 2;
                focus_pos = focus_pos_V;
            } else if(!strcmp(name, "R")){
                value = 4;
                focus_pos = focus_pos_R;
            } else if(!strcmp(name, "Pol")){
                value = 8;
                focus_pos = focus_pos_Clear;
            } else if(!strcmp(name, "B+Pol")){
                value = 1 + 8;
                focus_pos = focus_pos_B;
            } else if(!strcmp(name, "V+Pol")){
                value = 2 + 8;
                focus_pos = focus_pos_V;
            } else if(!strcmp(name, "R+Pol")){
                value = 4 + 8;
                focus_pos = focus_pos_R;
            }
        }

        if(focus_pos >= 0){
            /* Here we assume that focus_zero is already calibrated */
            int shift = focus_pos - (focus_current - focus_zero);

            /* FIXME: we will send this command also, but will not wait for its completion */
            hw_focus_move(hw, 0, shift);
        }

        hw_filters_set(hw, value);
        command_queue_add(hw->commands, connection, command_name(command), 5);
        command_queue_done(hw->commands, "set_filters");        
    } else if(command_match(command, "calibrate_filters")){
        hw_filters_calibrate(hw);
    } else if(command_match(command, "set_mirror")){
        int pos0 = 0;
        int pos1 = 0;

        command_args(command, "pos0=%d", &pos0, "pos1=%d", &pos1, NULL);

        if(celostate_pos0 != pos0 || celostate_pos1 != pos1){
            hw_mirror_set(hw, pos0, pos1);
            command_queue_add(hw->commands, connection, command_name(command), 5);
        } else
            server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "get_hw_status")){
        char *filter_name = "Clear";

        if(filters_current == 0)
            filter_name = "Clear";
        else if(filters_current == 1)
            filter_name = "B";
        else if(filters_current == 2)
            filter_name = "V";
        else if(filters_current == 4)
            filter_name = "R";
        else if(filters_current == 8)
            filter_name = "Pol";
        else if(filters_current == 1+8)
            filter_name = "B+Pol";
        else if(filters_current == 2+8)
            filter_name = "V+Pol";
        else if(filters_current == 4+8)
            filter_name = "R+Pol";
        else
            filter_name = "Custom";

        server_connection_message(connection,
                                  "hw_status connected=%d focus=%d focus2=%d ii_power=%d cover=%d"
                                  " camera=%d"
                                  " celostate_pos0=%d celostate_pos1=%d"
                                  " celostate_real0=%d celostate_real1=%d"
                                  " camera_temperature=%g camera_humidity=%g camera_dewpoint=%g"
                                  " celostate_temperature=%g celostate_humidity=%g celostate_dewpoint=%g"
                                  " filter0=%d filter1=%d filter2=%d filter3=%d filters=%d filter_name=%s lamp=%d"
                                  " focused=%d",
                                  hw->lowlevel_connection->is_connected && 1e-3*time_interval(last_status_time, time_current()) < 1,
                                  focus_current - focus_zero, focus2_current, ii_current, cover_current,
                                  camera_current,
                                  celostate_pos0, celostate_pos1,
                                  celostate_real0, celostate_real1,
                                  camera_temperature, camera_humidity, camera_dewpoint,
                                  celostate_temperature, celostate_humidity, celostate_dewpoint,
                                  filters_state[0], filters_state[1], filters_state[2], filters_state[3], filters_current, filter_name, lamp_current,
                                  focus_pos_Clear >= 0 && focus_pos_B >= 0 && focus_pos_V >= 0 && focus_pos_R >= 0);
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    hw_str *hw = (hw_str *)data;

    if(connection == hw->logger_connection){
        server_connection_message(connection, "iam id=hw%d", hw->id);
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    hw_str *hw = (hw_str *)data;

    command_queue_remove_with_connection(hw->commands, connection);

    server_default_connection_connected_hook(server, connection, NULL);
}

int main(int argc, char **argv)
{
    hw_str *hw = (hw_str *)malloc(sizeof(hw_str));
    char *lowlevel_host = "localhost";
    int lowlevel_port = PORT_CHANNEL_LOWLEVEL;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;
    int port = PORT_CHANNEL_HW;

    hw->id = 0;

    parse_args(argc, argv,
               "lowlevel_host=%s", &lowlevel_host,
               "lowlevel_port=%d", &lowlevel_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "port=%d", &port,
               "id=%d", &hw->id,
               NULL);

    check_license(argv[0]);

    hw->server = server_create();
    hw->is_quit = FALSE;

    hw->lowlevel_connection = server_add_connection(hw->server, lowlevel_host, lowlevel_port);
    hw->lowlevel_connection->is_active = TRUE;
    hw->lowlevel_connection->data_read_hook = process_hw_data;
    hw->lowlevel_connection->data_read_hook_data = hw;
    server_add_timer(hw->server, 0.1, 0, hw_request_state_cb, hw);
    server_add_timer(hw->server, 1, 0, hw_keep_ii_cb, hw);

    hw->logger_connection = server_add_connection(hw->server, logger_host, logger_port);
    hw->logger_connection->is_active = TRUE;

    SERVER_SET_HOOK(hw->server, line_read_hook, process_command, hw);
    SERVER_SET_HOOK(hw->server, connection_connected_hook, connection_connected, hw);
    SERVER_SET_HOOK(hw->server, connection_disconnected_hook, connection_disconnected, hw);

    server_listen(hw->server, "localhost", port);

    hw->commands = command_queue_create();

    last_status_time = time_zero();
    last_error_time = time_zero();

    /* Initialize the focus */
    hw_focus_reset(hw, 0);

    /* Read stored focus values */
    restore_focus_pos(hw);

    while(!hw->is_quit){
        server_cycle(hw->server, 1);
        /* Check for sent commands with expired timers */
        command_queue_check(hw->commands);
    }

    command_queue_delete(hw->commands);
    server_delete(hw->server);
    free(hw);

    return EXIT_SUCCESS;
}
