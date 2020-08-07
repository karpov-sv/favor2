#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils.h"

#include "beholder.h"
#include "ports.h"
#include "command.h"
#include "script.h"
#include "coords.h"

#include "license.h"

static void command_queue_null_callback(char *, void *);
static void command_queue_timeout_callback(char *, void *);
static void process_command(server_str *, connection_str *, char *, void *);

beholder_str *beholder_create()
{
    beholder_str *beholder = (beholder_str *)malloc(sizeof(beholder_str));

    beholder->server = server_create();

    beholder->weather_connection = NULL;
    beholder->dome_connection = NULL;
    beholder->scheduler_connection = NULL;
    beholder->can_connection = NULL;

    beholder->weather_status = NULL;
    beholder->dome_status = NULL;
    beholder->scheduler_status = NULL;
    beholder->scheduler_suggestion = NULL;
    beholder->can_status = NULL;

    beholder->is_night = TRUE;
    beholder->is_weather_good = TRUE;
    beholder->is_dome_open = TRUE;

    beholder->force_night = FALSE;

    beholder->nmounts = 0;
    beholder->nchannels = 0;

    beholder->mount = NULL;
    beholder->channel = NULL;

    beholder->script = NULL;

    beholder->commands = command_queue_create();
    beholder->commands->handler = command_queue_null_callback;
    beholder->commands->handler_data = beholder;
    beholder->commands->timeout_callback = command_queue_timeout_callback;
    beholder->commands->timeout_callback_data = beholder;

    beholder->db = NULL;

    beholder->state = NULL;

    beholder->is_quit = FALSE;

    return beholder;
}

void beholder_delete(beholder_str *beholder)
{
    int d;

    if(!beholder)
        return;

    if(beholder->weather_status)
        free(beholder->weather_status);
    if(beholder->dome_status)
        free(beholder->dome_status);
    if(beholder->scheduler_status)
        free(beholder->scheduler_status);
    if(beholder->scheduler_suggestion)
        free(beholder->scheduler_suggestion);
    if(beholder->can_status)
        free(beholder->can_status);

    if(beholder->commands)
        command_queue_delete(beholder->commands);

    if(beholder->db)
        db_delete(beholder->db);

    if(beholder->script)
        script_delete(beholder->script);

    for(d = 0; d < beholder->nchannels; d++)
        channel_delete(beholder->channel[d]);
    for(d = 0; d < beholder->nmounts; d++)
        mount_delete(beholder->mount[d]);

    free(beholder->channel);
    free(beholder->mount);

    if(beholder->state)
        free(beholder->state);

    server_delete(beholder->server);

    free(beholder);
}

mount_str *mount_create(int id)
{
    mount_str *mount = (mount_str *)malloc(sizeof(mount_str));

    mount->connection = NULL;
    mount->id = id;

    mount->ra = 0;
    mount->dec = 0;

    mount->state = 0;
    mount->status = NULL;

    dprintf("Mount %d created\n", mount->id);

    return mount;
}

void mount_delete(mount_str *mount)
{
    if(mount->status)
        free(mount->status);

    if(mount)
        free(mount);
}

channel_str *channel_create(beholder_str *beholder, int id)
{
    channel_str *channel = (channel_str *)malloc(sizeof(channel_str));

    channel->connection = NULL;
    channel->id = id;

    channel->mount = beholder->mount[(int)floor((id - 1)/2)];

    channel->state = 0;
    channel->status = NULL;
    channel->is_requesting_frame = FALSE;

    channel->frame = NULL;
    channel->frame_length = 0;
    channel->frame_time = time_current();

    dprintf("Channel %d, mounted on mount %d, created\n", channel->id, channel->mount->id);

    return channel;
}

void channel_delete(channel_str *channel)
{
    if(channel->status)
        free(channel->status);

    if(channel->frame)
        free(channel->frame);

    if(channel)
        free(channel);
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
    int d;

    for(d = 0; d < beholder->nmounts; d++)
        if(connection == beholder->mount[d]->connection){
            beholder->mount[d]->connection = NULL;
            dprintf("Mount %d disconnected\n", beholder->mount[d]->id);
        }

    for(d = 0; d < beholder->nchannels; d++)
        if(connection == beholder->channel[d]->connection){
            beholder->channel[d]->connection = NULL;

            free_and_null(beholder->channel[d]->frame);
            beholder->channel[d]->frame_length = 0;
            beholder->channel[d]->frame_time = time_zero();
            beholder->channel[d]->is_requesting_frame = FALSE;

            dprintf("Channel %d disconnected\n", beholder->channel[d]->id);
        }

    server_default_connection_disconnected_hook(server, connection, NULL);
}

static void channel_binary_read_hook(server_str *server, connection_str *connection, char *buffer, int length, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    channel_str *channel = NULL;
    int id = -1;
    int d;

    for(d = 0; d < beholder->nchannels; d++)
        if(beholder->channel[d]->connection == connection){
            id = d;
            break;
        }

    if(id < 0 || id >= beholder->nchannels)
        return;

    channel = beholder->channel[id];

    channel->frame = realloc(channel->frame, length);
    memcpy(channel->frame, buffer, length);

    channel->frame_length = length;
    channel->frame_time = time_current();

    channel->is_requesting_frame = FALSE;
}

void beholder_log_with_id(beholder_str *beholder, char *id, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log (%s): %s\n", id, buffer);

    if(beholder->logger_connection)
        server_connection_message(beholder->logger_connection, "log id=%s %s", id, buffer);

    free(buffer);
}

void beholder_log(beholder_str *beholder, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    if(beholder->logger_connection)
        server_connection_message(beholder->logger_connection, buffer);

    free(buffer);
}

char *make_status_string(beholder_str *beholder)
{
    char *status = make_string("beholder_status");
    int d;

    add_to_string(&status, " state=%s time=%d", beholder->state, time_unix(time_current()));
    add_to_string(&status, " is_night=%d is_weather_good=%d is_dome_open=%d", (beholder->is_night || beholder->force_night), beholder->is_weather_good, beholder->is_dome_open);
    add_to_string(&status, " nmounts=%d nchannels=%d", beholder->nmounts, beholder->nchannels);

    add_to_string(&status, " weather=%d dome=%d scheduler=%d can=%d",
                  beholder->weather_connection && beholder->weather_connection->is_connected,
                  beholder->dome_connection && beholder->dome_connection->is_connected,
                  beholder->scheduler_connection && beholder->scheduler_connection->is_connected,
                  beholder->can_connection && beholder->can_connection->is_connected);

    if(beholder->weather_status)
        add_to_string(&status, " %s", beholder->weather_status);
    if(beholder->dome_status)
        add_to_string(&status, " %s", beholder->dome_status);
    if(beholder->scheduler_status)
        add_to_string(&status, " %s", beholder->scheduler_status);
    if(beholder->scheduler_suggestion)
        add_to_string(&status, " %s", beholder->scheduler_suggestion);
    if(beholder->can_status)
        add_to_string(&status, " %s", beholder->can_status);

    for(d = 0; d < beholder->nmounts; d++){
        int connected = beholder->mount[d]->connection && beholder->mount[d]->connection->is_connected;

        add_to_string(&status, " mount%d=%d", beholder->mount[d]->id, connected);
        if(beholder->mount[d]->status)
            add_to_string(&status, " %s", beholder->mount[d]->status);
    }

    for(d = 0; d < beholder->nchannels; d++){
        int connected = beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected;

        add_to_string(&status, " channel%d=%d", beholder->channel[d]->id, connected);
        if(beholder->channel[d]->status)
            add_to_string(&status, " %s", beholder->channel[d]->status);
    }

    return status;
}

static void send_beholder_status(beholder_str *beholder, connection_str *connection)
{
    char *status = make_status_string(beholder);

    server_connection_message(connection, status);

    free(status);
}

static void command_queue_timeout_callback(char *name, void *data)
{
    beholder_str *beholder = (beholder_str *)data;

    beholder_log(beholder, "Command timeout: %s", name);
}

static void command_queue_null_callback(char *message, void *data)
{
    beholder_str *beholder = (beholder_str *)data;

    /* We treat all commands in queue with NULL connection as originating from
       the script. So let's return the result back to the script. If any. */
    if(beholder->script)
        script_message(beholder->script, message);
    else
        process_command(beholder->server, NULL, message, beholder);
}

static char *get_command_source(beholder_str *beholder, connection_str *connection)
{
    char *result = NULL;

    if(connection == beholder->dome_connection)
        result = make_string("dome");
    else if(connection == beholder->weather_connection)
        result = make_string("weather");
    else if(connection == beholder->scheduler_connection)
        result = make_string("scheduler");
    else if(connection == beholder->logger_connection)
        result = make_string("logger");
    else {
        int d;

        for(d = 0; d < beholder->nmounts; d++)
            if(connection == beholder->mount[d]->connection){
                result = make_string("mount%d", beholder->mount[d]->id);
                break;
            }

        for(d = 0; d < beholder->nchannels; d++)
            if(connection == beholder->channel[d]->connection){
                result = make_string("channel%d", beholder->channel[d]->id);
                break;
            }
    }

    return result;
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    command_str *command = command_parse(string);

    //server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        beholder->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_beholder_status")){
        send_beholder_status(beholder, connection);
    } else if(command_match(command, "iam")){
        char *class = NULL;
        int id = 0;

        command_args(command,
                     "class=%s", &class,
                     "id=%d", &id,
                     NULL);

        if(!strcmp(class, "mount") && id > 0 && id <= beholder->nmounts){
            beholder->mount[id - 1]->connection = connection;
            /* TODO: mount initialization */
            dprintf("Mount %d connected\n", id);
        }

        if(!strcmp(class, "channel") && id > 0 && id <= beholder->nchannels){
            beholder->channel[id - 1]->connection = connection;
            beholder->channel[id - 1]->is_requesting_frame = FALSE;
            beholder->channel[id - 1]->connection->binary_read_hook = channel_binary_read_hook;
            beholder->channel[id - 1]->connection->binary_read_hook_data = beholder;

            free_and_null(beholder->channel[id - 1]->frame);
            beholder->channel[id - 1]->frame_length = 0;

            /* TODO: channel initialization */
            dprintf("Channel %d connected\n", id);
        }

        if(class)
            free(class);
    } else if(command_match(command, "scheduler_status")){
        double zsun = 0;

        command_args(command, "zsun=%lf", &zsun, NULL);

        beholder->is_night = zsun > 90 + 10;

        if(beholder->force_night)
            beholder->is_night = TRUE;

        free_and_null(beholder->scheduler_status);
        beholder->scheduler_status = command_params_with_prefix(command, "scheduler");
    } else if(command_match(command, "can_status")){
        free_and_null(beholder->can_status);
        beholder->can_status = command_params_with_prefix(command, "can");
    } else if(command_match(command, "weather_status")){
        int cloud_cond = 0;
        int wind_cond = 0;
        int rain_cond = 0;
        int day_cond = 0;

        int weather_status = FALSE;

        command_args(command,
                     "cloud_cond=%d", &cloud_cond,
                     "wind_cond=%d", &wind_cond,
                     "rain_cond=%d", &rain_cond,
                     "day_cond=%d", &day_cond,
                     NULL);

        if(cloud_cond == 0 && wind_cond == 0 && rain_cond == 0 && day_cond == 0)
            /* Allsky has just restarted and is reporting all zeros */
            dprintf("Weather values are all zeros");
        else {
            weather_status = /* (cloud_cond == 1) && */ (wind_cond == 1 || wind_cond == 2) && (rain_cond == 1 || rain_cond == 2);

            if(beholder->is_weather_good != weather_status){
                beholder_log(beholder, "Weather state is %d", weather_status);
                beholder_log(beholder, "Status string is: %s", command->string);
            }

            beholder->is_weather_good = weather_status;

            free_and_null(beholder->weather_status);
            beholder->weather_status = command_params_with_prefix(command, "weather");
        }
    } else if(command_match(command, "dome_status")){
        int state = -1;

        command_args(command,
                     "state=%d", &state,
                     NULL);

        beholder->is_dome_open = state == 3;

        free_and_null(beholder->dome_status);
        beholder->dome_status = command_params_with_prefix(command, "dome");
    } else if(command_match(command, "mount_status")){
        int id = -1;

        command_args(command,
                     "id=%d", &id,
                     NULL);

        if(id > 0 && id <= beholder->nmounts){
            char *lua_state = command_params_as_lua(command);

            free_and_null(beholder->mount[id - 1]->status);
            beholder->mount[id - 1]->status = command_params_with_prefix(command, "mount%d", id);

            script_command(beholder->script, "mount_state[%d]=%s", beholder->channel[id - 1]->id, lua_state);

            free(lua_state);
        }
    } else if(command_match(command, "channel_status")){
        int id = -1;
        int state = -1;
        double ra = 0;
        double dec = 0;

        command_args(command,
                     "id=%d", &id,
                     "state=%d", &state,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     NULL);

        if(id > 0 && id <= beholder->nchannels){
            char *lua_state = command_params_as_lua(command);

            free_and_null(beholder->channel[id - 1]->status);
            beholder->channel[id - 1]->status = command_params_with_prefix(command, "channel%d", id);
            beholder->channel[id - 1]->state = state;
            beholder->channel[id - 1]->ra = ra;
            beholder->channel[id - 1]->dec = dec;

            script_command(beholder->script, "channel_state[%d]=%s", beholder->channel[id - 1]->id, lua_state);

            free(lua_state);
        }
    } else if(command_match(command, "broadcast_mounts") || command_match(command, "command_mounts")){
        char *string = command_params(command);
        int d;

        for(d = 0; d < beholder->nmounts; d++)
            if(beholder->mount[d]->connection && beholder->mount[d]->connection->is_connected)
                server_connection_message(beholder->mount[d]->connection, string);
        if(string)
            free(string);
    } else if(command_match(command, "broadcast_channels") || command_match(command, "command_channels")){
        char *string = command_params(command);
        int d;

        for(d = 0; d < beholder->nchannels; d++)
            if(beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected)
                server_connection_message(beholder->channel[d]->connection, string);
        if(string)
            free(string);
    } else if(command_match(command, "broadcast_odd_channels") || command_match(command, "command_odd_channels")){
        char *string = command_params(command);
        int d;

        for(d = 0; d < beholder->nchannels; d++)
            if(beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected &&
               beholder->channel[d]->id % 2 == 1)
                server_connection_message(beholder->channel[d]->connection, string);
        if(string)
            free(string);
    } else if(command_match(command, "broadcast_even_channels") || command_match(command, "command_even_channels")){
        char *string = command_params(command);
        int d;

        for(d = 0; d < beholder->nchannels; d++)
            if(beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected &&
               beholder->channel[d]->id % 2 == 0)
                server_connection_message(beholder->channel[d]->connection, string);
        if(string)
            free(string);
    } else if(command_match(command, "command_channel")){
        char *string = command_params_n(command, 1);
        int id = 0;

        command_args(command,
                     "id=%d", &id,
                     NULL);

        if(id > 0){
            int d;

            for(d = 0; d < beholder->nchannels; d++)
                if(beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected &&
                   beholder->channel[d]->id == id)
                    server_connection_message(beholder->channel[d]->connection, string);
        }

        if(string)
            free(string);
    } else if(command_match(command, "command_mount")){
        char *string = command_params_n(command, 1);
        int id = 0;

        command_args(command,
                     "id=%d", &id,
                     NULL);

        if(id > 0){
            int d;

            for(d = 0; d < beholder->nmounts; d++)
                if(beholder->mount[d]->connection && beholder->mount[d]->connection->is_connected &&
                   beholder->mount[d]->id == id)
                    server_connection_message(beholder->mount[d]->connection, string);
        }

        if(string)
            free(string);
    } else if(command_match(command, "command_weather")){
        char *string = command_params(command);

        if(beholder->weather_connection && beholder->weather_connection->is_connected)
                server_connection_message(beholder->weather_connection, string);

        if(string)
            free(string);
    } else if(command_match(command, "command_dome")){
        char *string = command_params(command);

        if(beholder->dome_connection && beholder->dome_connection->is_connected)
                server_connection_message(beholder->dome_connection, string);

        if(string)
            free(string);
    } else if(command_match(command, "command_can")){
        char *string = command_params(command);

        if(beholder->can_connection && beholder->can_connection->is_connected)
                server_connection_message(beholder->can_connection, string);

        if(string)
            free(string);
    } else if(command_match(command, "command_scheduler")){
        char *string = command_params(command);

        if(beholder->scheduler_connection && beholder->scheduler_connection->is_connected)
                server_connection_message(beholder->scheduler_connection, string);

        if(string)
            free(string);
    } else if(command_match(command, "schedule")){
        server_connection_message(beholder->scheduler_connection, "get_pointing_suggestion");
        command_queue_add(beholder->commands, connection, command_name(command), 10);
    } else if(command_match(command, "scheduler_pointing_suggestion") || command_match(command, "next_target")){
        double ra = 0;
        double dec = 0;
        char *name = NULL;
        char *type = NULL;
        int id = 0;
        double exposure = 1200;
        double priority = 1.0;
        int repeat = 1;
        char *filter = make_string("Clear");
        char *uuid = NULL;

        command_args(command,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "name=%s", &name,
                     "type=%s", &type,
                     "id=%d", &id,
                     "exposure=%lf", &exposure,
                     "priority=%lf", &priority,
                     "repeat=%d", &repeat,
                     "filter=%s", &filter,
                     "uuid=%s", &uuid,
                     NULL);

        if(command_queue_contains(beholder->commands, "schedule") || command_match(command, "next_target")){
            dprintf("Next pointing suggested: ra=%g dec=%g, name=%s type=%s exposure=%g repeat=%d filter=%s\n",
                    ra, dec, name, type, exposure, repeat, filter);

            script_set_number(beholder->script, "target_ra", ra);
            script_set_number(beholder->script, "target_dec", dec);
            script_set_string(beholder->script, "target_name", name);
            script_set_string(beholder->script, "target_type", type);
            script_set_number(beholder->script, "target_id", id);
            script_set_number(beholder->script, "target_exposure", exposure);
            script_set_number(beholder->script, "target_priority", priority);
            script_set_number(beholder->script, "target_repeat", repeat);
            script_set_string(beholder->script, "target_filter", filter);
            script_set_string(beholder->script, "target_uuid", uuid);

            command_queue_done(beholder->commands, "schedule");
        }

        free(name);
        free(type);
        free(filter);
        if(uuid)
            free(uuid);

        free_and_null(beholder->scheduler_suggestion);
        beholder->scheduler_suggestion = command_params_with_prefix(command, "scheduler_suggestion");
    } else if(command_match(command, "add_coverage")){
        /* Coverage info from channel, let's forward it to scheduler */
        server_connection_message(beholder->scheduler_connection, string);
    } else if(command_match(command, "add_coverage_done")){
        /* Do nothing */
    } else if(command_match(command, "target_observed") || command_match(command, "target_completed")){
        /* Target observed by the channel, let's forward it to scheduler */
        server_connection_message(beholder->scheduler_connection, string);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_state")){
        char *state = NULL;

        command_args(command, "state=%s", &state, NULL);

        free_and_null(beholder->state);
        beholder->state = make_string("%s", state);

        if(state)
            free(state);
    } else if(command_match(command, "set_param")){
        command_args(command,
                     "night=%d", &beholder->is_night,
                     "weather_good=%d", &beholder->is_weather_good,
                     "dome_open=%d", &beholder->is_dome_open,
                     NULL);
    } else if(command_match(command, "force_night")){
        beholder->force_night = TRUE;

        command_args(command,
                     "night=%d", &beholder->force_night,
                     NULL);
    } else if(command_match(command, "current_frame")){
        int length = 0;

        command_args(command, "length=%d", &length, NULL);

        server_connection_wait_for_binary(connection, length);
    } else if(command_match(command, "get_current_frame")){
        int id = 0;

        command_args(command,
                     "id=%d", &id,
                     NULL);

        if(id > 0 && id <= beholder->nchannels && beholder->channel[id - 1]->frame_length){
            server_connection_message(connection, "current_frame id=%d length=%d format=jpeg", id, beholder->channel[id - 1]->frame_length);
            server_connection_write_block(connection, beholder->channel[id - 1]->frame, beholder->channel[id - 1]->frame_length);
        } else
            server_connection_message(connection, "get_current_frame_error id=%d", id);
    } else if(command_match(command, "get_current_frame_done")){
    } else if(command_match(command, "get_current_frame_timeout")){
        int d;

        for(d = 0; d < beholder->nchannels; d++)
            if(connection == beholder->channel[d]->connection)
                beholder->channel[d]->is_requesting_frame = FALSE;
    } else if(command_match(command, "reschedule")){
        /* It may be either normal reschedule request or external trigger */
        int id = 0;
        char *uuid = NULL;
        double ra = -1;
        double dec = 0;
        double priority = 0;
        int channel_id = 0;

        command_args(command,
                     "priority=%lf", &priority,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "id=%d", &id,
                     "uuid=%s", &uuid,
                     NULL);

        if(priority > 1.0 && ra >= 0 && script_get_boolean(beholder->script, "may_followup_alerts")){
            /* Possible candidate for in-FOV follow-up, we have to check
               whether it is actually inside any of channels now */
            int d;
            double min_dist = 100.0;

            for(d = 0; d < beholder->nchannels; d++){
                if(script_command(beholder->script, "return channel_guard_fn(%d)", beholder->channel[d]->id) &&
                   script_get_boolean_result(beholder->script)){
                    double dist = coords_sky_distance(ra, dec, beholder->channel[d]->ra, beholder->channel[d]->dec);

                    dprintf("channel %d: dist=%g\n", beholder->channel[d]->id, dist);

                    if(dist < min_dist && dist < 5.0){
                        min_dist = dist;
                        channel_id = beholder->channel[d]->id;
                    }
                }
            }
        }

        if(channel_id > 0){
            /* In-FOV follow-up */

            if(beholder->db){
                time_str time = time_current();
                char *timestamp = time_str_get_date_time(time);
                time_str evening_time = time_incremented(time, -12*60*60);
                char *night = make_string("%04d_%02d_%02d", evening_time.year, evening_time.month, evening_time.day);
                PGresult *res = db_query(beholder->db, "INSERT INTO alerts (channel_id, ra, dec, excess, time, night) VALUES (%d, %g, %g, %g, '%s', '%s') RETURNING id", channel_id, ra, dec, 0.0, timestamp, night);

                if(PQntuples(res))
                    script_set_number(beholder->script, "alert_id", db_get_int(res, 0, 0));

                PQclear(res);
                free(night);
                free(timestamp);
            }

            script_set_number(beholder->script, "alert_ra", ra);
            script_set_number(beholder->script, "alert_dec", dec);
            script_set_number(beholder->script, "alert_channel_id", channel_id);

            script_message(beholder->script, "alert");
        }

        /* Now we may just pass the original message to the script */
        script_message(beholder->script, string);

        if(uuid)
            free(uuid);
    } else if(command_match(command, "alert")){
        int channel_id = 0;
        double ra = 0;
        double dec = 0;
        double excess = 0;
        u_int64_t time = 0;
        char *timestamp = NULL;

        command_args(command,
                     "channel_id=%d", &channel_id,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "excess=%lf", &excess,
                     "time=%lld", &time,
                     NULL);

        timestamp = time_str_get_date_time(time_str_from_uuid(time));

        if(channel_id && beholder->db){
            time_str evening_time = time_incremented(time_str_from_uuid(time), -12*60*60);
            char *night = make_string("%04d_%02d_%02d", evening_time.year, evening_time.month, evening_time.day);
            PGresult *res = db_query(beholder->db, "INSERT INTO alerts (channel_id, ra, dec, excess, time, night) VALUES (%d, %g, %g, %g, '%s', '%s') RETURNING id", channel_id, ra, dec, excess, timestamp, night);

            if(PQntuples(res))
                script_set_number(beholder->script, "alert_id", db_get_int(res, 0, 0));

            PQclear(res);
            free(night);
        }

        script_set_number(beholder->script, "alert_ra", ra);
        script_set_number(beholder->script, "alert_dec", dec);
        script_set_number(beholder->script, "alert_channel_id", channel_id);

        script_message(beholder->script, string);

        free(timestamp);
    } else {
        /* Let's pass everything else to the script */
        char *source = get_command_source(beholder, connection);

        if(source){
            script_message_from(beholder->script, source, string);
            free(source);
        } else
            script_message(beholder->script, string);
    }

    command_delete(command);
}

static void process_script_message_global(char *string, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    command_str *command = command_parse(string);

    dprintf("Message from script (global): %s\n", string);

    /* Dispatch the command */
    process_command(beholder->server, NULL, string, beholder);

    command_delete(command);
}

static void process_script_message_to(char *target, char *string, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    command_str *tgt = command_parse(target);
    connection_str *connection = NULL;
    int id = -1;

    dprintf("Message from script (%s): %s\n", target, string);

    /* Dispatch outgoing commands for subsystems */
    if(command_match(tgt, "dome")){
        connection = beholder->dome_connection;
    } else if(command_match(tgt, "scheduler")){
        connection = beholder->scheduler_connection;
    } else if(command_match(tgt, "weather")){
        connection = beholder->weather_connection;
    } else if(command_match(tgt, "logger")){
        connection = beholder->logger_connection;
    } else if(command_match(tgt, "can")){
        connection = beholder->can_connection;
    } else if(sscanf(target, "mount%d", &id) > 0){
        if(id > 0 && id <= beholder->nmounts)
            connection = beholder->mount[id - 1]->connection;
    } else if(sscanf(target, "channel%d", &id) > 0){
        if(id > 0 && id <= beholder->nchannels)
            connection = beholder->channel[id - 1]->connection;
    } else if(tgt)
        dprintf("Unknown script message target: %s for command: %s\n", target, string);

    if(connection)
        server_connection_message(connection, string);
    /* else */
    /*     process_command(beholder->server, NULL, string, beholder); */

    command_delete(tgt);
}

void update_status_timer_callback(server_str *server, int type, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    int d;

    if(beholder->scheduler_connection->is_connected)
        server_connection_message(beholder->scheduler_connection, "get_scheduler_status");
    if(beholder->weather_connection->is_connected)
        server_connection_message(beholder->weather_connection, "get_weather_status");
    if(beholder->dome_connection->is_connected)
        server_connection_message(beholder->dome_connection, "get_dome_status");
    if(beholder->can_connection->is_connected)
        server_connection_message(beholder->can_connection, "get_can_status");

    for(d = 0; d < beholder->nmounts; d++)
        if(beholder->mount[d]->connection && beholder->mount[d]->connection->is_connected)
            server_connection_message(beholder->mount[d]->connection, "get_mount_status");

    for(d = 0; d < beholder->nchannels; d++)
        if(beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected)
            server_connection_message(beholder->channel[d]->connection, "get_channel_status");

    server_add_timer(server, 0.1, 0, update_status_timer_callback, beholder);
}

void update_scheduler_suggestion_timer_callback(server_str *server, int type, void *data)
{
    beholder_str *beholder = (beholder_str *)data;

    if(beholder->scheduler_connection->is_connected)
        server_connection_message(beholder->scheduler_connection, "get_pointing_suggestion");

    server_add_timer(server, 10, 0, update_scheduler_suggestion_timer_callback, beholder);
}

void update_current_frames_callback(server_str *server, int type, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    int d;

    for(d = 0; d < beholder->nchannels; d++)
        if(beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected){
            if(!beholder->channel[d]->is_requesting_frame){
                server_connection_message(beholder->channel[d]->connection, "get_current_frame");
                beholder->channel[d]->is_requesting_frame = TRUE;
            } else if(1e-3*time_interval(beholder->channel[d]->frame_time, time_current()) > 5.0)
                /* Reset frame requesting flag for channels with no recent frames */
                beholder->channel[d]->is_requesting_frame = FALSE;
        }

    server_add_timer(server, 1.0, 0, update_current_frames_callback, beholder);
}

void update_db_status_callback(server_str *server, int type, void *data)
{
    beholder_str *beholder = (beholder_str *)data;
    char *status = make_status_string(beholder);
    command_str *command = command_parse(status);
    char *hstring = command_params_as_hstore(command);
    char *timestamp = time_str_get_date_time(time_current());

    if(beholder->db){
        PGresult *res = db_query(beholder->db, "INSERT INTO beholder_status (time, status) VALUES ('%s', %s);",
                                 timestamp, hstring);

        PQclear(res);
    }

    free(timestamp);
    free(hstring);
    command_delete(command);
    free(status);

    server_add_timer(server, 60.0, 0, update_db_status_callback, beholder);
}

void beholder_tick(beholder_str *beholder)
{
    int d;
    int nchannels = 0;
    int nmounts = 0;

    /* Count connected channels and mounts */
    for(d = 0; d < beholder->nchannels; d++){
        int connected = beholder->channel[d]->connection && beholder->channel[d]->connection->is_connected;

        script_command(beholder->script, "channel_connected[%d]=%s", beholder->channel[d]->id, connected ? "true" : "false");

        if(connected)
            nchannels ++;
    }

    for(d = 0; d < beholder->nmounts; d++){
        int connected = beholder->mount[d]->connection && beholder->mount[d]->connection->is_connected;

        script_command(beholder->script, "mount_connected[%d]=%s", beholder->mount[d]->id, connected ? "true" : "false");

        if(connected)
            nmounts ++;
    }

    script_set_number(beholder->script, "num_channels_connected", nchannels);
    script_set_number(beholder->script, "num_mounts_connected", nmounts);

    script_set_boolean(beholder->script, "is_night", beholder->is_night);
    script_set_boolean(beholder->script, "is_weather_good", beholder->is_weather_good);
    script_set_boolean(beholder->script, "is_dome_open", beholder->is_dome_open);

    script_message(beholder->script, "tick");
}

int main(int argc, char **argv)
{
    beholder_str *beholder = beholder_create();
    char *weather_host = "localhost";
    int weather_port = PORT_WEATHER;
    char *dome_host = "localhost";
    int dome_port = PORT_DOME;
    char *scheduler_host = "localhost";
    int scheduler_port = PORT_SCHEDULER;
    char *can_host = "localhost";
    int can_port = PORT_CANSERVER;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;
    int port = PORT_BEHOLDER;
    int d;

    beholder->nmounts = 5;
    beholder->nchannels = 9;

    parse_args(argc, argv,
               "weather_host=%s", &weather_host,
               "weather_port=%d", &weather_port,
               "dome_host=%s", &dome_host,
               "dome_port=%d", &dome_port,
               "scheduler_host=%s", &scheduler_host,
               "scheduler_port=%d", &scheduler_port,
               "can_host=%s", &can_host,
               "can_port=%d", &can_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "port=%d", &port,
               "nmounts=%d", &beholder->nmounts,
               "nchannels=%d", &beholder->nchannels,
               NULL);

    check_license(argv[0]);

    /* Constructor */
    server_listen(beholder->server, "localhost", port);
    SERVER_SET_HOOK(beholder->server, line_read_hook, process_command, beholder);
    SERVER_SET_HOOK(beholder->server, connection_connected_hook, connection_connected, beholder);
    SERVER_SET_HOOK(beholder->server, connection_disconnected_hook, connection_disconnected, beholder);

    beholder->weather_connection = server_add_connection(beholder->server, weather_host, weather_port);
    beholder->weather_connection->is_active = TRUE;

    beholder->dome_connection = server_add_connection(beholder->server, dome_host, dome_port);
    beholder->dome_connection->is_active = TRUE;

    beholder->scheduler_connection = server_add_connection(beholder->server, scheduler_host, scheduler_port);
    beholder->scheduler_connection->is_active = TRUE;

    beholder->can_connection = server_add_connection(beholder->server, can_host, can_port);
    beholder->can_connection->is_active = TRUE;

    beholder->logger_connection = server_add_connection(beholder->server, logger_host, logger_port);
    beholder->logger_connection->is_active = TRUE;

    beholder->mount = (mount_str **)malloc(sizeof(mount_str *)*beholder->nmounts);
    beholder->channel = (channel_str **)malloc(sizeof(channel_str *)*beholder->nchannels);

    for(d = 0; d < beholder->nmounts; d++)
        beholder->mount[d] = mount_create(d + 1);
    for(d = 0; d < beholder->nchannels; d++)
        beholder->channel[d] = channel_create(beholder, d + 1);

    /* Init the script. It will start right away. */
    beholder->script = script_create();
    beholder->script->message_callback = process_script_message_global;
    beholder->script->message_callback_data = beholder;
    beholder->script->message_to_callback = process_script_message_to;
    beholder->script->message_to_callback_data = beholder;

    script_set_number(beholder->script, "num_channels", beholder->nchannels);
    script_set_number(beholder->script, "num_mounts", beholder->nmounts);

    if(!script_load(beholder->script, "lua/beholder.lua")){
        dprintf("Can't load beholder script, exiting\n");
        return EXIT_FAILURE;
    }

    beholder->db = db_create("dbname=favor2");

    /* Run the status update timers */
    update_status_timer_callback(beholder->server, 0, beholder);
    update_scheduler_suggestion_timer_callback(beholder->server, 0, beholder);
    update_current_frames_callback(beholder->server, 0, beholder);

    /* Run the DB status storage timer */
    update_db_status_callback(beholder->server, 0, beholder);

    /* Main cycle */
    while(!beholder->is_quit){
        server_cycle(beholder->server, 10);

        beholder_tick(beholder);
    }

    /* Destructor */
    beholder_delete(beholder);

    return EXIT_SUCCESS;
}
