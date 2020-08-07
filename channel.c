#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>

#include "utils.h"

#include "command.h"
#include "channel.h"
#include "average.h"
#include "sextractor.h"
#include "match.h"
#include "realtime_types.h"
#include "license.h"

#include "ports.h"

/* Whether to correct web image for dark/flat */
#define PREPROCESS_WEB_IMAGE 1

int config_grabber_udp_port = 0;
char *config_grabber_host = NULL;
int config_grabber_port = 0;
char *config_db_host = NULL;
int config_db_port = 0;

static void process_script_message(char *, void *);
static void process_script_message_to(char *, char *, void *);
static void command_queue_null_callback(char *, void *);
static void command_queue_timeout_callback(char *, void *);

channel_str *channel_create(int port)
{
    channel_str *channel = (channel_str *)malloc(sizeof(channel_str));

    channel->id = 0;

    channel->server = server_create();
    server_listen(channel->server, NULL, port);

    channel->server_queue = queue_create(0);
    /* server thread will not wait for messages as it has to operate together
       with select() loop */
    channel->server_queue->timeout = 0;

    channel->grabber_queue = queue_create(100);
    channel->grabber_queue->timeout = 0;

    channel->realtime_queue = queue_create(100);
    channel->realtime_queue->timeout = 1;

    channel->secondscale_queue = queue_create(100);
    channel->secondscale_queue->timeout = 1;

    channel->archive_queue = queue_create(0);
    channel->archive_queue->timeout = 1;

    channel->raw_queue = queue_create(200);
    channel->raw_queue->timeout = 1;

    channel->db_queue = queue_create(0);
    channel->db_queue->timeout = 1;

    channel->hw_connection = NULL;
    channel->beholder_connection = NULL;
    channel->logger_connection = NULL;
    channel->mount_connection = NULL;

#ifdef ANDOR
    channel->andor_temperature = 0;
    channel->andor_temperaturestatus = 0;
    channel->andor_exposure = 0;
    channel->andor_framerate = 0;
    channel->andor_binning = 0;
    channel->andor_filter = 0;
    channel->andor_preamp = 0;
    channel->andor_shutter = 0;
#endif

    /* FIXME: these should be set by BEHOLDER on connect, if necessary */
    channel->realtime_state = STATE_OFF;
    channel->raw_state = STATE_OFF;
    channel->grabber_state = STATE_OFF;

    channel->is_focused = FALSE;

    channel->state = CHANNEL_STATE_NORMAL;

    channel->is_quit = FALSE;

    channel->hw_connected = 0;
    channel->hw_focus = -1;
    channel->hw_focus2 = -1;
    channel->hw_ii_power = -1;
    channel->hw_cover = -1;
    channel->hw_lamp = -1;
    channel->hw_camera = -1;
    channel->hw_filter = NULL;
    channel->hw_mirror0 = 0;
    channel->hw_mirror1 = 0;

    channel->hw_camera_temperature = 0;
    channel->hw_camera_humidity = 0;
    channel->hw_camera_dewpoint = 0;
    channel->hw_celostate_temperature = 0;
    channel->hw_celostate_humidity = 0;
    channel->hw_celostate_dewpoint = 0;

    channel->ra0 = 0;
    channel->dec0 = 0;
    channel->delta_ra = 0;
    channel->delta_dec = 0;
    channel->ra = 0;
    channel->dec = 0;
    channel->coords = coords_empty();
    channel->image_width = 0;
    channel->image_height = 0;

    channel->image_mean = 0;
    channel->image_std = 0;
    channel->image_median = 0;
    channel->image_mad = 0;

    channel->realtime_nrecords = 0;
    channel->realtime_nsingle = 0;
    channel->realtime_ndouble = 0;
    channel->realtime_nmeteor = 0;
    channel->realtime_nmoving = 0;
    channel->realtime_nflash = 0;

    channel->secondscale_nobjects = 0;
    channel->secondscale_mag0 = 0;
    channel->secondscale_mag_scale = 0;
    channel->secondscale_mag_min = 0;
    channel->secondscale_mag_max = 0;
    channel->secondscale_mag_limit = 0;

    channel->pixscale = 25.0;
    channel->target_name = NULL;
    channel->target_type = NULL;
    channel->target_id = 0;
    channel->target_filter = NULL;
    channel->target_exposure = 0;
    channel->target_uuid = NULL;

    channel->commands = command_queue_create();
    channel->commands->handler = command_queue_null_callback;
    channel->commands->handler_data = channel;
    channel->commands->timeout_callback = command_queue_timeout_callback;
    channel->commands->timeout_callback_data = channel;

    channel->script = script_create();
    channel->script->message_callback = process_script_message;
    channel->script->message_callback_data = channel;
    channel->script->message_to_callback = process_script_message_to;
    channel->script->message_to_callback_data = channel;

    channel->image = NULL;

    channel->accum_image = NULL;
    channel->accum_length = 0;
    channel->accum_exposure = 0;

    channel->progress = 0;

    channel->dark = NULL;
    channel->flat = NULL;
    channel->mask = NULL;

    channel->filepath = "AVG";
    channel->rawpath = "IMG";

    channel->mirror0_dra = 0;
    channel->mirror0_ddec = 0;
    channel->mirror1_dra = 0;
    channel->mirror1_ddec = 0;
    channel->mirror_dra = 0;
    channel->mirror_ddec = 0;

    channel->last_image_time = time_zero();

    channel->has_ii = FALSE;

    channel->is_zoom = FALSE;
    channel->scale = 2;

    pthread_mutex_init(&channel->mutex, NULL);

    return channel;
}

void channel_delete(channel_str *channel)
{
    if(!channel)
        return;

    if(channel->script)
        script_delete(channel->script);

    if(channel->image)
        image_delete(channel->image);

    if(channel->accum_image)
        image_delete(channel->accum_image);

    if(channel->dark)
        image_delete(channel->dark);
    if(channel->flat)
        image_delete(channel->flat);

    /* We do not need to delete the mask as it will be freed inside realtime_delete() */
    /* image_delete(channel->mask); */

    queue_delete(channel->server_queue);
    queue_delete(channel->grabber_queue);
    queue_delete(channel->realtime_queue);
    queue_delete(channel->secondscale_queue);
    queue_delete(channel->archive_queue);
    queue_delete(channel->raw_queue);
    queue_delete(channel->db_queue);

    server_delete(channel->server);

    command_queue_delete(channel->commands);

    if(channel->target_name)
        free(channel->target_name);
    if(channel->target_type)
        free(channel->target_type);
    if(channel->target_filter)
        free(channel->target_filter);

    free(channel);
}

void channel_log(channel_str *channel, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    if(channel->logger_connection)// && channel->logger_connection->is_connected)
        server_connection_message(channel->logger_connection, buffer);

    free(buffer);
}

/* Annotate the image with various keywords */
void channel_annotate_image(channel_str *channel, image_str *image)
{
    image_keyword_add_int(image, "CHANNEL ID", channel->id, "Channel acquired the image");

    /* Store current hardware parameters */
    if(channel->hw_filter)
        image_keyword_add(image, "FILTER", channel->hw_filter, "Current filter");
    if(channel->has_ii)
        image_keyword_add_int(image, "II_POWER", channel->hw_ii_power, "Image Intensifier state");
    image_keyword_add_int(image, "LAMP", channel->hw_lamp, "Current flatfield lamp state");
    image_keyword_add_int(image, "COVER", channel->hw_cover, "Current cover state");
    image_keyword_add_int(image, "FOCUS", channel->hw_focus, "Current objective focus");
    if(channel->has_ii)
        image_keyword_add_int(image, "FOCUS2", channel->hw_focus2, "Current transmission optics focus");
    image_keyword_add_int(image, "MIRROR_POS0", channel->hw_mirror0, "Current celostate position");
    image_keyword_add_int(image, "MIRROR_POS1", channel->hw_mirror1, "Current celostate position");
}

void channel_store_image(channel_str *channel, image_str *image, char *type)
{
    /* TODO: avoid image copying by assuming strict usage patterns?.. */
    image_str *copy = image_copy(image);

    image_keyword_add(copy, "TYPE", type, "Image type");

    pthread_mutex_lock(&channel->mutex);

    /* We will store current target parameters only in target-observing regimes */
    if(channel->state == CHANNEL_STATE_MONITORING ||
       channel->state == CHANNEL_STATE_FOLLOWUP ||
       channel->state == CHANNEL_STATE_IMAGING ||
       channel->state == CHANNEL_STATE_NORMAL){
        /* Store current pointing parameters, if defined */
        if(channel->target_name)
            image_keyword_add(copy, "TARGET", channel->target_name, "Current target name");
        if(channel->target_type)
            image_keyword_add(copy, "TARGET TYPE", channel->target_type, "Current target type");
        if(channel->target_id)
            image_keyword_add_int(copy, "TARGET ID", channel->target_id, "Current target/plan ID");
        if(channel->target_uuid)
            image_keyword_add(copy, "TARGET UUID", channel->target_uuid, "Current target/plan UUID");
    }

    channel_annotate_image(channel, copy);

    pthread_mutex_unlock(&channel->mutex);

    if(coords_is_empty(&image->coords) &&
       strcmp(type, "dark") != 0 &&
       strcmp(type, "skyflat") != 0 &&
       strcmp(type, "flat") != 0 &&
       strcmp(type, "avg") != 0)
        /* 'Normal' image with no WCS info should be matched before storing */
        queue_add_with_destructor(channel->archive_queue, CHANNEL_MSG_IMAGE, copy, (void (*)(void *))image_delete);
    else
        queue_add_with_destructor(channel->db_queue, CHANNEL_MSG_IMAGE, copy, (void (*)(void *))image_delete);
}

char *channel_status_string(channel_str *channel)
{
    /* Image corners */
    double ra1 = channel->ra;
    double dec1 = channel->dec;
    double ra2 = channel->ra;
    double dec2 = channel->dec;
    double ra3 = channel->ra;
    double dec3 = channel->dec;
    double ra4 = channel->ra;
    double dec4 = channel->dec;

    pthread_mutex_lock(&channel->mutex);

    if(channel->realtime_state == STATE_ON && !coords_is_empty(&channel->coords)){
        /* Use actual coordinates provided by data processing subsystem to compute image corners */

        coords_get_ra_dec(&channel->coords, 0, 0, &ra1, &dec1);
        coords_get_ra_dec(&channel->coords, channel->image_width, 0, &ra2, &dec2);
        coords_get_ra_dec(&channel->coords, channel->image_width, channel->image_height, &ra3, &dec3);
        coords_get_ra_dec(&channel->coords, 0, channel->image_height, &ra4, &dec4);
    }

    char *string = make_string("channel_status state=%d processing=%d storage=%d grabber=%d "
                               "realtime_queue=%d secondscale_queue=%d archive_queue=%d raw_queue=%d grabber_queue=%d db_queue=%d "
                               "hw=%d hw_connected=%d "
                               "hw_focus=%d hw_focus2=%d hw_ii_power=%d hw_cover=%d hw_lamp=%d hw_camera=%d hw_filter=%s "
                               "hw_mirror0=%d hw_mirror1=%d "
                               "hw_camera_temperature=%g hw_camera_humidity=%g hw_camera_dewpoint=%g "
                               "hw_celostate_temperature=%g hw_celostate_humidity=%g hw_celostate_dewpoint=%g "
                               "ra=%g dec=%g ra0=%g dec0=%g delta_ra=%g delta_dec=%g "
                               "target=%s "
                               "last_frame_age=%g "
                               "progress=%g "
                               "is_focused=%d has_ii=%d is_zoom=%d "
                               "has_dark=%d has_flat=%d has_mask=%d "
                               "id=%d "
                               "image_mean=%g image_std=%g image_median=%g image_mad=%g "
#ifdef ANDOR
                               "andor=1 andor_exposure=%g andor_framerate=%g andor_temperature=%g andor_temperaturestatus=%d andor_binning=%d "
                               "andor_filter=%d andor_preamp=%d andor_shutter=%d "
#else
                               "andor=0 "
#endif
                               "target_name=%s target_type=%s target_exposure=%g target_filter=%s "
                               "realtime_nrecords=%d realtime_nsingle=%d realtime_ndouble=%d "
                               "realtime_nmeteor=%d realtime_nmoving=%d realtime_nflash=%d "
                               "secondscale_nobjects=%d secondscale_mag0=%g secondscale_mag_scale=%g "
                               "secondscale_mag_min=%g secondscale_mag_max=%g secondscale_mag_limit=%g "
                               "ra1=%g dec1=%g ra2=%g dec2=%g ra3=%g dec3=%g ra4=%g dec4=%g ",
                               channel->state, channel->realtime_state, channel->raw_state, channel->grabber_state,
                               queue_length(channel->realtime_queue), queue_length(channel->secondscale_queue), queue_length(channel->archive_queue),
                               queue_length(channel->raw_queue), queue_length(channel->grabber_queue), queue_length(channel->db_queue),
                               (channel->hw_connection && channel->hw_connection->is_connected), channel->hw_connected,
                               channel->hw_focus, channel->hw_focus2, channel->hw_ii_power, channel->hw_cover,
                               channel->hw_lamp, channel->hw_camera, channel->hw_filter,
                               channel->hw_mirror0, channel->hw_mirror1,
                               channel->hw_camera_temperature, channel->hw_camera_humidity, channel->hw_camera_dewpoint,
                               channel->hw_celostate_temperature, channel->hw_celostate_humidity, channel->hw_celostate_dewpoint,
                               channel->ra, channel->dec, channel->ra0, channel->dec0, channel->delta_ra, channel->delta_dec,
                               channel->target_name,
                               1e-3*time_interval(channel->last_image_time, time_current()),
                               channel->progress,
                               channel->is_focused, channel->has_ii, channel->is_zoom,
                               channel->dark != NULL, channel->flat != NULL, channel->mask != NULL,
                               channel->id,
                               channel->image_mean, channel->image_std, channel->image_median, channel->image_mad,
#ifdef ANDOR
                               channel->andor_exposure, channel->andor_framerate, channel->andor_temperature,
                               channel->andor_temperaturestatus, channel->andor_binning,
                               channel->andor_filter, channel->andor_preamp, channel->andor_shutter,
#endif
                               channel->target_name, channel->target_type, channel->target_exposure, channel->target_filter,
                               channel->realtime_nrecords, channel->realtime_nsingle, channel->realtime_ndouble,
                               channel->realtime_nmeteor, channel->realtime_nmoving, channel->realtime_nflash,
                               channel->secondscale_nobjects, channel->secondscale_mag0, channel->secondscale_mag_scale,
                               channel->secondscale_mag_min, channel->secondscale_mag_max, channel->secondscale_mag_limit,
                               ra1, dec1, ra2, dec2, ra3, dec3, ra4, dec4);

    pthread_mutex_unlock(&channel->mutex);

    return string;
}

static void get_channel_status(channel_str *channel, connection_str *connection)
{
    char *reply = channel_status_string(channel);

    server_connection_message(connection, reply);

    free(reply);
}

static void channel_update_coords(channel_str *channel)
{
    pthread_mutex_lock(&channel->mutex);

    if(channel->realtime_state == STATE_ON && !coords_is_empty(&channel->coords)){
        /* Use actual coordinates provided by data processing subsystem */
        coords_get_ra_dec(&channel->coords, 0.5*channel->image_width, 0.5*channel->image_height, &channel->ra, &channel->dec);

        coords_get_ra_dec_shift(channel->ra0, channel->dec0, channel->ra, channel->dec, &channel->delta_ra, &channel->delta_dec);
    } else {
        /* Derive coordinates from celostate position */
        channel->delta_ra = channel->mirror0_dra*channel->hw_mirror0 + channel->mirror1_dra*channel->hw_mirror1 + channel->mirror_dra;
        channel->delta_dec = channel->mirror0_ddec*channel->hw_mirror0 + channel->mirror1_ddec*channel->hw_mirror1 + channel->mirror_ddec;

        coords_get_ra_dec_shifted(channel->ra0, channel->dec0, channel->delta_ra, channel->delta_dec, &channel->ra, &channel->dec);
    }

    pthread_mutex_unlock(&channel->mutex);
}

static void channel_get_mirror_pos(channel_str *channel, double delta_ra, double delta_dec, int *pos0_ptr, int *pos1_ptr)
{
    double pos0 = 0;
    double pos1 = 0;
    double det = channel->mirror0_dra*channel->mirror1_ddec - channel->mirror0_ddec*channel->mirror1_dra;

    delta_ra -= channel->mirror_dra;
    delta_dec -= channel->mirror_ddec;

    /* Use the same formulae as in channel_update_coords() */
    if(det != 0){
        pos0 = (delta_ra*channel->mirror1_ddec - delta_dec*channel->mirror1_dra)/det;
        pos1 = (delta_dec*channel->mirror0_dra - delta_ra*channel->mirror0_ddec)/det;
    }

    /* Debug info */
    dprintf("delta_ra=%g delta_dec=%g -> pos0=%g pos1=%g\n", delta_ra, delta_dec, pos0, pos1);

    /* Restrict celostate positions to hardware limits */
    pos0 = MIN(MAX(pos0, -127), 127);
    pos1 = MIN(MAX(pos1, -127), 127);

    dprintf("recalc: %g %g\n",
            channel->mirror0_dra*pos0 + channel->mirror1_dra*pos1,
            channel->mirror0_ddec*pos0 + channel->mirror1_ddec*pos1);

    if(pos0_ptr)
        *pos0_ptr = pos0;
    if(pos1_ptr)
        *pos1_ptr = pos1;
}

static void command_queue_timeout_callback(char *name, void *data)
{
    channel_str *channel = (channel_str *)data;

    /* FIXME: temporary */
    if(strcmp(name, "get_current_frame") == 0)
        dprintf("Command timeout: get_current_frame\n");
    else
        channel_log(channel, "Command timeout: %s", name);
}

static int channel_script_message(channel_str *channel, char *message)
{
    if(channel->script)
        script_message(channel->script, message);

    return TRUE;
}

static void command_queue_null_callback(char *message, void *data)
{
    channel_str *channel = (channel_str *)data;

    /* We treat all commands in queue with NULL connection as originating from
       the script. So let's return the result back to the script. If any. */
    if(channel->script)
        script_message(channel->script, message);
    else
        process_command(channel->server, NULL, message, channel);
}

static char *get_command_source(channel_str *channel, connection_str *connection, command_str *command)
{
    char *name = command_name(command);
    char *result = NULL;

    /* */
    if(connection == channel->hw_connection)
        result = make_string("hw");
    else if(connection == channel->mount_connection)
        result = make_string("mount");
    else if(connection == channel->beholder_connection){
        /* Report 'beholder' as source only for replies, not for commands */
        if(strstr(name, "_done") ||
           strstr(name, "_timeout") ||
           strstr(name, "_error"))
            result = make_string("beholder");
    }

    return result;
}

void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    channel_str *channel = (channel_str *)data;
    command_str *command = command_parse(string);

    /* connection == NULL means internally generated command */
    /* if(connection) */
    /*     server_default_line_read_hook(server, connection, string, data); */
    /* else */
    /*     dprintf("Internal message: %s\n", string); */

    if(command_match(command, "exit") || command_match(command, "quit")){
        /* Clear queues so the message is not postponed */
        queue_clear(channel->grabber_queue);
        queue_clear(channel->realtime_queue);
        queue_clear(channel->secondscale_queue);
        queue_clear(channel->archive_queue);
        queue_clear(channel->raw_queue);
        queue_clear(channel->db_queue);
        /* Broadcast EXIT message */
        queue_add(channel->grabber_queue, CHANNEL_MSG_EXIT, NULL);
        queue_add(channel->realtime_queue, CHANNEL_MSG_EXIT, NULL);
        queue_add(channel->secondscale_queue, CHANNEL_MSG_EXIT, NULL);
        queue_add(channel->archive_queue, CHANNEL_MSG_EXIT, NULL);
        queue_add(channel->raw_queue, CHANNEL_MSG_EXIT, NULL);
        queue_add(channel->db_queue, CHANNEL_MSG_EXIT, NULL);
        channel->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "jpeg_params")){
        double min = 0.03;
        double max = 0.95;

        command_args(command,
                     "min=%lf", &min,
                     "max=%lf", &max,
                     NULL);

        image_jpeg_set_percentile(min, max);
    } else if(command_match(command, "set_zoom")){
        command_args(command, "zoom=%d", &channel->is_zoom, NULL);
    } else if(command_match(command, "reset_dark")){
        free_and_null(channel->dark);
    } else if(command_match(command, "reset_flat")){
        free_and_null(channel->flat);
    /* } else if(command_match(command, "set_focused")){ */
    /*     channel->is_focused = TRUE; */

    /*     command_args(command, */
    /*                  "focused=%d", &channel->is_focused, */
    /*                  NULL); */

    /*     script_set_boolean(channel->script, "is_focused", channel->is_focused); */
    } else if(command_match(command, "set_grabber")){
        char *copy = make_string(string);

        /* We will send the command to grabber thread verbatim */
        queue_add_with_destructor(channel->grabber_queue, CHANNEL_MSG_COMMAND, copy, free);
        command_queue_add(channel->commands, connection, command_name(command), 10);
    } else if(command_match(command, "hw")){
        char *cmd = command_params(command);

        /* Forward the command to HW */
        server_connection_message(channel->hw_connection, cmd);

        free(cmd);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "processing_start")){
        channel->coords = coords_empty();
        command_queue_add(channel->commands, connection, command_name(command), 120);
        queue_add(channel->realtime_queue, CHANNEL_MSG_PROCESSING_START, NULL);
    } else if(command_match(command, "processing_stop")){
        command_queue_add(channel->commands, connection, command_name(command), 120);
        /* Clear the realtime queue so the command will be received and processed instantly */
        queue_clear(channel->realtime_queue);
        queue_add(channel->realtime_queue, CHANNEL_MSG_PROCESSING_STOP, NULL);
    } else if(command_match(command, "storage_start")){
        command_queue_add(channel->commands, connection, command_name(command), 120);
        queue_add(channel->raw_queue, CHANNEL_MSG_STORAGE_START, NULL);
    } else if(command_match(command, "storage_stop")){
        command_queue_add(channel->commands, connection, command_name(command), 120);
        /* Clear the queue so the command will be received and processed instantly */
        queue_clear(channel->raw_queue);
        queue_add(channel->raw_queue, CHANNEL_MSG_STORAGE_STOP, NULL);
    } else if(command_match(command, "secondscale_clear") || command_match(command, "secondscale_stop")){
        queue_clear(channel->secondscale_queue);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "archive_clear") || command_match(command, "archive_stop")){
        queue_clear(channel->archive_queue);
        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "grabber_start")){
        command_queue_add(channel->commands, connection, command_name(command), 1);
        queue_add(channel->grabber_queue, CHANNEL_MSG_GRABBER_START, NULL);
    } else if(command_match(command, "grabber_stop")){
        command_queue_add(channel->commands, connection, command_name(command), 1);
        queue_add(channel->grabber_queue, CHANNEL_MSG_GRABBER_STOP, NULL);
    } else if(command_match(command, "get_channel_status")){
        get_channel_status(channel, connection);
    } else if(command_match(command, "hw_status")){
        char *filter = NULL;

        pthread_mutex_lock(&channel->mutex);

        command_args(command,
                     "connected=%d", &channel->hw_connected,
                     "focused=%d", &channel->is_focused,
                     "focus=%d", &channel->hw_focus,
                     "focus2=%d", &channel->hw_focus2,
                     "ii_power=%d", &channel->hw_ii_power,
                     "cover=%d", &channel->hw_cover,
                     "camera=%d", &channel->hw_camera,
                     "lamp=%d", &channel->hw_lamp,
                     "filter_name=%s", &filter,
                     "celostate_pos0=%d", &channel->hw_mirror0,
                     "celostate_pos1=%d", &channel->hw_mirror1,
                     "camera_temperature=%lf", &channel->hw_camera_temperature,
                     "camera_humidity=%lf", &channel->hw_camera_humidity,
                     "camera_dewpoint=%lf", &channel->hw_camera_dewpoint,
                     "celostate_temperature=%lf", &channel->hw_celostate_temperature,
                     "celostate_humidity=%lf", &channel->hw_celostate_humidity,
                     "celostate_dewpoint=%lf", &channel->hw_celostate_dewpoint,
                     NULL);

        if(filter){
            free_and_null(channel->hw_filter);
            channel->hw_filter = make_string(filter);
        }

        pthread_mutex_unlock(&channel->mutex);

        channel_update_coords(channel);
    } else if(command_match(command, "mount_status")){
        command_args(command,
                     "ra=%lf", &channel->ra0,
                     "dec=%lf", &channel->dec0,
                     NULL);

        channel_update_coords(channel);
    } else if(command_match(command, "get_current_frame")) {
        /* FIXME: We can't handle concurrent queries with different scales */
        command_args(command, "scale=%d", &channel->scale, NULL);

        command_queue_add(channel->commands, connection, command_name(command), 1);
    } else if(command_match(command, "clear_target")){
        free_and_null(channel->target_name);
        free_and_null(channel->target_type);
        free_and_null(channel->target_uuid);
        free_and_null(channel->target_filter);
        channel->target_exposure = 0;
        channel->target_id = 0;

        script_set_string(channel->script, "target_name", channel->target_name);
        script_set_string(channel->script, "target_type", channel->target_type);
        script_set_number(channel->script, "target_id", channel->target_id);
        script_set_number(channel->script, "target_exposure", channel->target_exposure);
        script_set_number(channel->script, "target_length", 0);
        script_set_string(channel->script, "target_filter", channel->target_filter);
        script_set_string(channel->script, "target_uuid", channel->target_uuid);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "next_target")){
        char *name = NULL;
        char *type = NULL;
        char *filter = NULL;
        char *uuid = NULL;
        int id = 0;
        double exposure = 0;
        int repeat = 1;

        double ra = -999;
        double dec = -999;
        double delta_ra = 0;
        double delta_dec = 0;
        int pos0 = 0;
        int pos1 = 0;

        command_args(command,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "delta_ra=%lf", &delta_ra,
                     "delta_dec=%lf", &delta_dec,
                     "name=%s", &name,
                     "type=%s", &type,
                     "id=%d", &id,
                     "exposure=%lf", &exposure,
                     "repeat=%d", &repeat,
                     "filter=%s", &filter,
                     "uuid=%s", &uuid,
                     NULL);

        free_and_null(channel->target_name);
        free_and_null(channel->target_type);
        free_and_null(channel->target_uuid);
        free_and_null(channel->target_filter);

        channel->target_name = name ? name : make_string("unknown");
        channel->target_type = type ? type : make_string("unknown");
        channel->target_uuid = uuid;
        channel->target_filter = filter ? filter : make_string("Clear");
        channel->target_exposure = exposure;
        channel->target_repeat = repeat;
        channel->target_id = id;

        script_set_string(channel->script, "target_name", channel->target_name);
        script_set_string(channel->script, "target_type", channel->target_type);
        script_set_number(channel->script, "target_id", channel->target_id);
        script_set_number(channel->script, "target_exposure", channel->target_exposure);
        script_set_number(channel->script, "target_repeat", channel->target_repeat);
        script_set_string(channel->script, "target_filter", channel->target_filter);
        script_set_string(channel->script, "target_uuid", channel->target_uuid);

        channel_log(channel, "Next target: name=%s type=%s exposure=%g filter=%s id=%d",
                    channel->target_name, channel->target_type, channel->target_exposure, channel->target_filter,
                    channel->target_id);

        if(ra != -999 && dec != -999){
            /* Absolute coordinates provided, we should compute relative ones */
            /* Here we assume that channel already knows its current position */
            coords_get_ra_dec_shift(channel->ra0, channel->dec0, ra, dec, &delta_ra, &delta_dec);

            channel_log(channel, "Next target: ra=%g dec=%g", ra, dec);
            channel_log(channel, "Current pointing: ra0=%g dec0=%g", channel->ra0, channel->dec0);
        }

        channel_get_mirror_pos(channel, delta_ra, delta_dec, &pos0, &pos1);

        channel_log(channel, "Relative repointing: delta_ra=%g delta_dec=%g pos0=%d pos1=%d", delta_ra, delta_dec, pos0, pos1);

        script_set_number(channel->script, "target_mirror0", pos0);
        script_set_number(channel->script, "target_mirror1", pos1);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_mirror")){
        double delta_ra = -100;
        double delta_dec = -100;
        int pos0 = 0;
        int pos1 = 0;

        command_args(command,
                     "pos0=%d", &pos0,
                     "pos1=%d", &pos1,
                     "delta_ra=%lf", &delta_ra,
                     "delta_dec=%lf", &delta_dec,
                     NULL);

        if(delta_ra >  -100 || delta_dec > -100)
            channel_get_mirror_pos(channel, delta_ra, delta_dec, &pos0, &pos1);

        server_connection_message(channel->hw_connection, "set_mirror %d %d\n", pos0, pos1);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_state")){
        char *state = "normal";

        command_args(command, "state=%s", &state, NULL);

        if(!strcmp(state, "normal"))
            channel->state = CHANNEL_STATE_NORMAL;
        else if(!strcmp(state, "write_darks"))
            channel->state = CHANNEL_STATE_WRITE_DARKS;
        else if(!strcmp(state, "write_flats"))
            channel->state = CHANNEL_STATE_WRITE_FLATS;
        else if(!strcmp(state, "autofocus"))
            channel->state = CHANNEL_STATE_AUTOFOCUS;
        else if(!strcmp(state, "monitoring"))
            channel->state = CHANNEL_STATE_MONITORING;
        else if(!strcmp(state, "followup"))
            channel->state = CHANNEL_STATE_FOLLOWUP;
        else if(!strcmp(state, "imaging"))
            channel->state = CHANNEL_STATE_IMAGING;
        else if(!strcmp(state, "locate"))
            channel->state = CHANNEL_STATE_LOCATE;
        else if(!strcmp(state, "reset"))
            channel->state = CHANNEL_STATE_RESET;
        else if(!strcmp(state, "calibrate"))
            channel->state = CHANNEL_STATE_CALIBRATE;
        else if(!strcmp(state, "error"))
            channel->state = CHANNEL_STATE_ERROR;

        channel_log(channel, "Channel state: %s", state);
    } else if(command_match(command, "command_done")){
        char *name = NULL;

        command_args(command, "command=%s", &name, NULL);

        if(name)
            command_queue_done(channel->commands, name);
    } else if(command_match(command, "unknown_command")){
        /* Don't do anything here to prevent an avalanche */
    } else {
        /* Let's pass everything else to the script */
        char *source = get_command_source(channel, connection, command);

        if(command_match(command, "write_darks") ||
           command_match(command, "write_flats") ||
           command_match(command, "autofocus") ||
           command_match(command, "followup") ||
           command_match(command, "imaging") ||
           command_match(command, "monitoring_start") ||
           command_match(command, "monitoring_stop") ||
           command_match(command, "reset") ||
           command_match(command, "calibrate_celostate") ||
           command_match(command, "locate"))
            command_queue_add(channel->commands, connection, command_name(command), 5*3600);

        if(source){
            script_message_from(channel->script, source, string);
            free(source);
        } else
            script_message(channel->script, string);
    }

    command_delete(command);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    channel_str *channel = (channel_str *)data;

    if(connection == channel->beholder_connection){
        server_connection_message(connection, "iam class=channel id=%d", channel->id);
    }

    if(connection == channel->logger_connection){
        server_connection_message(connection, "iam id=channel%d", channel->id);
    }

    if(connection == channel->hw_connection){
        script_set_boolean(channel->script, "is_hw_connected", TRUE);
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    channel_str *channel = (channel_str *)data;

    if(connection == channel->hw_connection){
        script_set_boolean(channel->script, "is_hw_connected", FALSE);
    }

    /* Remove from command queue all commands belonging to this connection, or
       else command_queue_done will fail trying to send the reply to it */
    /* TODO: BTW, maybe another solution would be to mark such "lost"
       connections as NULLs, and handle them specially later?.. */
    command_queue_remove_with_connection(channel->commands, connection);

    server_default_connection_disconnected_hook(server, connection, NULL);
}

static void dispatch_image(channel_str *channel, image_str *image)
{
    command_sent_str *command = NULL;

    pthread_mutex_lock(&channel->mutex);
    /* Store the time of last image we got */
    channel->last_image_time = image->time;
    pthread_mutex_unlock(&channel->mutex);

    /* FIXME: all this stuff related to channel->image should be redesigned to avoid unnecessary data copying */
    if(channel->image)
        image_delete(channel->image);
    channel->image = image;

    channel_update_coords(channel);

    channel->image->coords.ra0 = channel->ra;
    channel->image->coords.dec0 = channel->dec;

    /* Add various keywords */
    channel_annotate_image(channel, image);

    /* Send image to clients awaiting for it */
    {
        char *data = NULL;
        int length = 0;
        double mean = 0;

        foreach(command, channel->commands->commands_sent)
            if(!strcmp(command->name, "get_current_frame")){
                if(!data){
#ifdef PREPROCESS_WEB_IMAGE
                    image_str *processed = image_create_double(image->width, image->height);;
                    int d;

                    if(channel->dark && channel->flat && 0)
                        for(d = 0; d < image->width*image->height; d++)
                            processed->double_data[d] = (image->data[d] - channel->dark->double_data[d])/channel->flat->double_data[d];
                    else if(channel->dark)
                        for(d = 0; d < image->width*image->height; d++)
                            processed->double_data[d] = image->data[d] - channel->dark->double_data[d];
                    else
                        for(d = 0; d < image->width*image->height; d++)
                            processed->double_data[d] = image->data[d];
#else
                    image_str *processed = image;
#endif
                    image_jpeg_set_scale(channel->scale);

                    if(channel->is_zoom){
                        image_str *cut = image_crop(processed, image->width*0.4, image->height*0.4, image->width*0.6, image->height*0.6);

                        image_jpeg_set_scale(1);
                        image_convert_to_jpeg(cut, (unsigned char **)&data, &length);

                        image_delete(cut);
                    } else {
                        image_jpeg_set_scale(channel->scale);
                        image_convert_to_jpeg(processed, (unsigned char **)&data, &length);
                    }

                    channel->image_mean = get_mean(processed->double_data, processed->width*processed->height, &channel->image_std);
                    channel->image_median = get_median_mad(processed->double_data, processed->width*processed->height, &channel->image_mad);

#ifdef PREPROCESS_WEB_IMAGE
                    image_delete(processed);
#endif
                }

                if(length){
                    server_connection_message(command->connection, "current_frame length=%d format=jpeg mean=%g", length, channel->image_mean);
                    server_connection_write_block(command->connection, data, length);
                }
            }

        if(data)
            free(data);
    }

    command_queue_done(channel->commands, "get_current_frame");

    /* Probably the script wish to do something with the image? */
    if(channel->script)
        channel_script_message(channel, "image_acquired");

    pthread_mutex_lock(&channel->mutex);

    /* If grabber is not operational ... */
    if(channel->grabber_state == STATE_ERROR){
        pthread_mutex_unlock(&channel->mutex);
        image_delete_and_null(channel->image);
        return;
    }

    pthread_mutex_unlock(&channel->mutex);

    /* Process the data if necessary */
    if(queue_length(channel->realtime_queue) < channel->realtime_queue->maximal_length - 10)
        queue_add_with_destructor(channel->realtime_queue, CHANNEL_MSG_IMAGE,
                                  channel->image, (void (*)(void *))image_delete);
    else
        /* If realtime queue is too busy, send it directly to storage */
        queue_add_with_destructor(channel->raw_queue, CHANNEL_MSG_IMAGE,
                                  channel->image, (void (*)(void *))image_delete);

    /* Access to the image is unreliable here even if it is not deleted */
    channel->image = NULL;
}

static void channel_load_dark(channel_str *channel, char *filename)
{
    dprintf("Loading DARK frame: %s\n", filename);

    image_delete_and_null(channel->dark);
    channel->dark = image_create_from_fits(filename);

    /* Dark frame should be of DOUBLE type */
    if(channel->dark && channel->dark->type != IMAGE_DOUBLE){
        dprintf("Wrong format of DARK frame: %s\n", filename);
        image_delete_and_null(channel->dark);
    }

    if(channel->dark){
        queue_add_with_destructor(channel->realtime_queue, CHANNEL_MSG_IMAGE_DARK,
                                  image_copy(channel->dark), (void (*)(void *))image_delete);
        queue_add_with_destructor(channel->secondscale_queue, CHANNEL_MSG_IMAGE_DARK,
                                  image_copy(channel->dark), (void (*)(void *))image_delete);
        queue_add_with_destructor(channel->archive_queue, CHANNEL_MSG_IMAGE_DARK,
                                  image_copy(channel->dark), (void (*)(void *))image_delete);
    }
}

static void channel_load_flat(channel_str *channel, char *filename)
{
    dprintf("Loading FLAT frame: %s\n", filename);

    image_delete_and_null(channel->flat);
    channel->flat = image_create_from_fits(filename);

    /* Flat frame should be of DOUBLE type */
    if(channel->flat && channel->flat->type != IMAGE_DOUBLE){
        dprintf("Wrong format of FLAT frame: %s\n", filename);
        image_delete_and_null(channel->flat);
    }

    if(channel->flat){
        queue_add_with_destructor(channel->realtime_queue, CHANNEL_MSG_IMAGE_FLAT,
                                  image_copy(channel->flat), (void (*)(void *))image_delete);
        queue_add_with_destructor(channel->secondscale_queue, CHANNEL_MSG_IMAGE_FLAT,
                                  image_copy(channel->flat), (void (*)(void *))image_delete);
        queue_add_with_destructor(channel->archive_queue, CHANNEL_MSG_IMAGE_FLAT,
                                  image_copy(channel->flat), (void (*)(void *))image_delete);
    }
}

static void channel_report_object(channel_str *channel, object_str *object)
{
    double excess = 0;
    record_str *record;
    double ra = 0;
    double dec = 0;
    double x = 0;
    double y = 0;
    int N = 0;

    foreach(record, object->records){
        excess += record->excess;
        x += record->x;
        y += record->y;

        N ++;
    }

    x = x*1./N;
    y = y*1./N;
    excess = excess*1./N;

    record = list_first_item(object->records);

    coords_get_ra_dec(&object->coords, x, y, &ra, &dec);

    server_connection_message(channel->beholder_connection,
                              "alert channel_id=%d ra=%g dec=%g excess=%g time=%lld",
                              channel->id, ra, dec, excess, time_str_get_uuid(record->time));
}

static void process_queue_message(channel_str *channel, queue_message_str m)
{
    switch(m.event){
    case CHANNEL_MSG_IMAGE:
        /* It will be sent to realtime and to storage there, if necessary */
        dispatch_image(channel, (image_str *)m.data);
        break;
    case CHANNEL_MSG_FILENAME_DARK:
        channel_load_dark(channel, (char *)m.data);
        if(m.data)
            free(m.data);
        break;
    case CHANNEL_MSG_FILENAME_FLAT:
        channel_load_flat(channel, (char *)m.data);
        if(m.data)
            free(m.data);
        break;
    case CHANNEL_MSG_FRAME_INFO:
        if(m.data){
            frame_info_str *info = (frame_info_str *)m.data;
            double ra0 = 0;
            double dec0 = 0;
            double pixscale = coords_get_pixscale(&info->coords);

            coords_get_ra_dec(&info->coords, 0.5*info->width, 0.5*info->height, &ra0, &dec0);

            channel->coords = info->coords;
            channel->image_width = info->width;
            channel->image_height = info->height;

            server_connection_message(channel->beholder_connection, "add_coverage ra0=%g dec0=%g size_ra=%g size_dec=%g exposure=%g time=%d",
                                      ra0, dec0, pixscale*info->width, pixscale*info->height, info->exposure, (int)time_unix(info->time));
            free(m.data);
        }
        break;
    case CHANNEL_MSG_PROCESSING_STARTED:
        command_queue_done(channel->commands, "processing_start");
        break;
    case CHANNEL_MSG_PROCESSING_STOPPED:
        command_queue_done(channel->commands, "processing_stop");
        break;
    case CHANNEL_MSG_STORAGE_STARTED:
        command_queue_done(channel->commands, "storage_start");
        break;
    case CHANNEL_MSG_STORAGE_STOPPED:
        command_queue_done(channel->commands, "storage_stop");
        break;
    case CHANNEL_MSG_GRABBER_STARTED:
        command_queue_done(channel->commands, "grabber_start");
        break;
    case CHANNEL_MSG_GRABBER_STOPPED:
        command_queue_done(channel->commands, "grabber_stop");
        break;
    case CHANNEL_MSG_COMMAND_DONE:
        if(m.data){
            if(strcmp((char *)m.data, "grabber") == 0)
                command_queue_done(channel->commands, "set_grabber");

            free(m.data);
        }
        break;
    case CHANNEL_MSG_OBJECT:
        channel_report_object(channel, (object_str *)m.data);
        object_delete((object_str *)m.data);
        break;
    default:
        /* No messages received */
        break;
    }
}

static void hw_timer_cb(server_str *server, int type, void *data)
{
    channel_str *channel = (channel_str *)data;

    if(channel->hw_connection && channel->hw_connection->is_connected)
        server_connection_message(channel->hw_connection, "get_hw_status");

    server_add_timer(channel->server, 0.1, 0, hw_timer_cb, channel);
}

static void mount_timer_cb(server_str *server, int type, void *data)
{
    channel_str *channel = (channel_str *)data;

    if(channel->mount_connection && channel->mount_connection->is_connected)
        server_connection_message(channel->mount_connection, "get_mount_status");

    server_add_timer(channel->server, 0.1, 0, mount_timer_cb, channel);
}

static void script_timer_cb(server_str *server, int type, void *data)
{
    channel_str *channel = (channel_str *)data;

    if(channel->script){
        pthread_mutex_lock(&channel->mutex);

        script_set_number(channel->script, "realtime_state", channel->realtime_state);
        script_set_number(channel->script, "raw_state", channel->raw_state);
        script_set_number(channel->script, "grabber_state", channel->grabber_state);

        pthread_mutex_unlock(&channel->mutex);

        script_message(channel->script, "tick");
    }

    server_add_timer(channel->server, 0.1, 0, script_timer_cb, channel);
}

static void accum_image_prepare(channel_str *channel)
{
    image_delete_and_null(channel->accum_image);
    channel->accum_length = 0;
    channel->accum_exposure = 0;
}

static void accum_image_append(channel_str *channel, image_str *image)
{
    if(image){
        if(!channel->accum_image){
            channel->accum_image = image_create_double(image->width, image->height);
            image_copy_properties(image, channel->accum_image);
        }

        image_add(channel->accum_image, image);
        channel->accum_length ++;
        channel->accum_exposure += image_keyword_get_double(image, "EXPOSURE");
    }
}

static void accum_image_normalize(channel_str *channel)
{
    int d;

    for(d = 0; d < channel->accum_image->width*channel->accum_image->height; d++)
        channel->accum_image->double_data[d] *= 1./channel->accum_length;
}

static void accum_image_to_dark(channel_str *channel)
{
    accum_image_normalize(channel);

    image_keyword_add_int(channel->accum_image, "AVERAGED", channel->accum_length, "Total number of frames coadded");
    image_keyword_add_double(channel->accum_image, "TOTAL EXPOSURE", channel->accum_exposure, "Total exposure of coadded frames");
    image_keyword_add_double(channel->accum_image, "EXPOSURE", channel->accum_exposure/channel->accum_length, "Normalized (average) exposure");

    channel_store_image(channel, channel->accum_image, "dark");

    /* Set the image as our new dark frame */
    image_delete_and_null(channel->dark);
    channel->dark = image_copy(channel->accum_image);
}

static void accum_image_to_flat(channel_str *channel)
{
    accum_image_normalize(channel);

    image_keyword_add_int(channel->accum_image, "AVERAGED", channel->accum_length, "Total number of frames coadded");
    image_keyword_add_double(channel->accum_image, "TOTAL EXPOSURE", channel->accum_exposure, "Total exposure of coadded frames");
    image_keyword_add_double(channel->accum_image, "EXPOSURE", channel->accum_exposure/channel->accum_length, "Normalized (average) exposure");

    channel_store_image(channel, channel->accum_image, "flat");

    /* Set the image as our new dark frame */
    image_delete_and_null(channel->flat);
    channel->flat = image_copy(channel->accum_image);
}

static void accum_image_to_skyflat(channel_str *channel)
{
    accum_image_normalize(channel);

    image_keyword_add_int(channel->accum_image, "AVERAGED", channel->accum_length, "Total number of frames coadded");
    image_keyword_add_double(channel->accum_image, "TOTAL EXPOSURE", channel->accum_exposure, "Total exposure of coadded frames");
    image_keyword_add_double(channel->accum_image, "EXPOSURE", channel->accum_exposure/channel->accum_length, "Normalized (average) exposure");

    channel_store_image(channel, channel->accum_image, "skyflat");
}

static void accum_image_to_file(channel_str *channel, char *filename)
{
    if(channel->accum_length > 1){
        /* Normalize the image and modify the header accordingly */
        accum_image_normalize(channel);

        image_keyword_add_int(channel->accum_image, "AVERAGED", channel->accum_length, "Total number of frames coadded");
        image_keyword_add_double(channel->accum_image, "TOTAL EXPOSURE", channel->accum_exposure, "Total exposure of coadded frames");
        image_keyword_add_double(channel->accum_image, "EXPOSURE", channel->accum_exposure/channel->accum_length, "Normalized (average) exposure");

        image_dump_to_fits(channel->accum_image, filename);
    } else {
        /* Single image. We will revert it back to integers */
        image_str *new = image_create(channel->accum_image->width, channel->accum_image->height);

        image_copy_properties(channel->accum_image, new);
        image_add(new, channel->accum_image);

        image_dump_to_fits(new, filename);

        image_delete(new);
    }
}

static void accum_image_to_focus(channel_str *channel)
{
    struct list_head stars;
    double quality = 0;

    accum_image_normalize(channel);

    sextractor_get_stars_list(channel->accum_image, 100000, &stars);

    quality = list_length(&stars);

    script_set_number(channel->script, "focus_quality", quality);

    free_list(stars);
}

static void accum_image_to_db(channel_str *channel, char *type, int count)
{
    if(channel->accum_length > 1){
        /* Normalize the image and modify the header accordingly */
        accum_image_normalize(channel);

        image_keyword_add_int(channel->accum_image, "AVERAGED", channel->accum_length, "Total number of frames coadded");
        image_keyword_add_double(channel->accum_image, "TOTAL EXPOSURE", channel->accum_exposure, "Total exposure of coadded frames");
        image_keyword_add_double(channel->accum_image, "EXPOSURE", channel->accum_exposure/channel->accum_length, "Normalized (average) exposure");

        channel_store_image(channel, channel->accum_image, type);
    } else {
        /* Single image. We will revert it back to integers */
        image_str *new = image_create(channel->accum_image->width, channel->accum_image->height);

        image_copy_properties(channel->accum_image, new);
        image_add(new, channel->accum_image);

        image_keyword_add_int(new, "COUNT", count, "Position in a sequence");

        channel_store_image(channel, new, type);

        image_delete(new);
    }
}

static void accum_image_to_locate(channel_str *channel)
{
    double ra0 = 0;
    double dec0 = 0;
    coords_str coords;

    accum_image_normalize(channel);

    coords = blind_match_coords(channel->accum_image);

    if(!coords_is_empty(&coords)){
        coords_get_ra_dec(&coords, 0.5*channel->accum_image->width, 0.5*channel->accum_image->height, &ra0, &dec0);

        /* FIXME: for now, disable fixing coordinates in polar region */
        if(dec0 > 80)
            return;

        if(channel->mount_connection && channel->mount_connection->is_connected)
            server_connection_message(channel->mount_connection, "fix ra=%g dec=%g", ra0, dec0);
    }
}

static void accum_image_to_getpos(channel_str *channel, int is_dist)
{
    double ra = 0;
    double dec = 0;
    coords_str coords;

    static double ra0 = 0;
    static double dec0 = 0;

    accum_image_normalize(channel);

    coords = blind_match_coords(channel->accum_image);

    coords_get_ra_dec(&coords, 0.5*channel->accum_image->width, 0.5*channel->accum_image->height, &ra, &dec);

    if(!is_dist){
        /* Store center position to static variables and to script */
        ra0 = ra;
        dec0 = dec;

        script_set_number(channel->script, "sky_ra", ra);
        script_set_number(channel->script, "sky_dec", dec);
    } else {
        /* Get the distance from mount position */
        double dist = coords_sky_distance(ra, dec, channel->ra0, channel->dec0);
        double delta_ra = 0;
        double delta_dec = 0;

        coords_get_ra_dec_shift(channel->ra0, channel->dec0, ra, dec, &delta_ra, &delta_dec);

        script_set_number(channel->script, "sky_ra", ra);
        script_set_number(channel->script, "sky_dec", dec);
        script_set_number(channel->script, "sky_delta_ra", delta_ra);
        script_set_number(channel->script, "sky_delta_dec", delta_dec);
        script_set_number(channel->script, "sky_distance", dist);
    }
}

void process_script_message(char *string, void *data)
{
    channel_str *channel = (channel_str *)data;
    command_str *command = command_parse(string);
    char *timestamp = time_str_get_date_time(time_current());

    dprintf("Message from script at %s: %s\n", timestamp, string);

    if(command_match(command, "accum_image_prepare")){
        accum_image_prepare(channel);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_append")){
        accum_image_append(channel, channel->image);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_dark")){
        accum_image_to_dark(channel);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_flat")){
        accum_image_to_flat(channel);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_skyflat")){
        accum_image_to_skyflat(channel);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_file")){
        char *filename = "/tmp/out_accum.fits";

        command_args(command, "filename=%s", &filename, NULL);

        accum_image_to_file(channel, filename);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_focus")){
        accum_image_to_focus(channel);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_db")){
        char *type = "target";
        int count = 0;

        command_args(command,
                     "type=%s", &type,
                     "count=%d", &count,
                     NULL);

        accum_image_to_db(channel, type, count);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_locate")){
        accum_image_to_locate(channel);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_getpos")){
        accum_image_to_getpos(channel, FALSE);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "accum_image_to_getdist")){
        accum_image_to_getpos(channel, TRUE);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "state_done")){
        char *name = NULL;

        command_args(command, "state=%s", &name, NULL);

        channel_log(channel, "State done: %s", name);

        /* Perform state-specific operations */
        if(strcmp(name, "followup") == 0){
            /* Target observation finished */
        } else if(strcmp(name, "autofocus") == 0){
            /* Autofocusing finished */
        }

        if(name)
            command_queue_done(channel->commands, name);
        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "set_progress")){
        double value = 0.0;
        double max = 1.0;

        command_args(command,
                     "progress=%lf", &value,
                     "max=%lf", &max,
                     NULL);

        channel->progress = value / max;

        script_message(channel->script, "%s_done", command_name(command));
    } else if(command_match(command, "set_ii_power") ||
              command_match(command, "reset_focus") ||
              command_match(command, "move_focus") ||
              command_match(command, "set_cover") ||
              command_match(command, "set_filters") ||
              command_match(command, "set_mirror")){
        /* Forward these commands directly to HW subsystem */
        server_connection_message(channel->hw_connection, string);
    } else {
        process_command(channel->server, NULL, string, channel);
    }

    free(timestamp);
    command_delete(command);
}

void process_script_message_to(char *target, char *string, void *data)
{
    channel_str *channel = (channel_str *)data;
    char *timestamp = time_str_get_date_time(time_current());
    command_str *command = command_parse(string);

    dprintf("Message from script to %s at %s: %s\n", target, timestamp, string);

    /* Process some specific commands */
    if(command_match(command, "set_mirror")){
        double delta_ra = -100;
        double delta_dec = -100;
        int pos0 = 0;
        int pos1 = 0;

        command_args(command,
                     "pos0=%d", &pos0,
                     "pos1=%d", &pos1,
                     "delta_ra=%lf", &delta_ra,
                     "delta_dec=%lf", &delta_dec,
                     NULL);

        if(delta_ra >  -100 || delta_dec > -100)
            channel_get_mirror_pos(channel, delta_ra, delta_dec, &pos0, &pos1);

        server_connection_message(channel->hw_connection, "set_mirror %d %d\n", pos0, pos1);
    } else if(!strcmp(target, "channel"))
        process_script_message(string, data);
    else if(!strcmp(target, "hw"))
        server_connection_message(channel->hw_connection, string);
    else if(!strcmp(target, "beholder"))
        server_connection_message(channel->beholder_connection, string);
    else if(!strcmp(target, "logger"))
        channel_log(channel, string);
    else
        dprintf("Unknown script message target: %s\n", target);

    free(timestamp);
    command_delete(command);
}

int main(int argc, char **argv)
{
    channel_str *channel = NULL;
    pthread_t grabber_thread;
    pthread_t realtime_thread;
    pthread_t secondscale_thread;
    pthread_t archive_thread;
    pthread_t raw_thread;
    pthread_t db_thread;
    int port = PORT_CHANNEL_CONTROL;
    int udp_port = PORT_GRABBER_UDP;
    char *hw_host = "localhost";
    int hw_port = PORT_CHANNEL_HW;
    char *grabber_host = "localhost";
    int grabber_port = PORT_GRABBER_CONTROL;
    char *beholder_host = "localhost";
    int beholder_port = PORT_BEHOLDER;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;
    char *mount_host = "localhost";
    int mount_port = PORT_MOUNT;
    char *db_host = "localhost";
    int db_port = 5432;
    char *mask_filename = "mask.realtime.fits";

    channel = channel_create(port);

    parse_args(argc, argv,
               "id=%d", &channel->id,
               "port=%d", &port,
               "udp_port=%d", &udp_port,
               "hw_host=%s", &hw_host,
               "hw_port=%d", &hw_port,
               "grabber_host=%s", &grabber_host,
               "grabber_port=%d", &grabber_port,
               "beholder_host=%s", &beholder_host,
               "beholder_port=%d", &beholder_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "mount_host=%s", &mount_host,
               "mount_port=%d", &mount_port,
               "db_host=%s", &db_host,
               "db_port=%d", &db_port,
               "file=%s", &channel->filepath,
               "raw=%s", &channel->rawpath,
               "ra=%lf", &channel->ra0,
               "dec=%lf", &channel->dec0,
               "pixscale=%lf", &channel->pixscale,
               "scale=%d", &channel->scale,
               "mask=%s", &mask_filename,
               "-ii", &channel->has_ii,
               "-focus", &channel->is_focused,
               NULL);

    check_license(argv[0]);

    /* Set up custom network command processing code */
    SERVER_SET_HOOK(channel->server, line_read_hook, process_command, channel);
    /* Set up custom network post-connection code */
    SERVER_SET_HOOK(channel->server, connection_connected_hook, connection_connected, channel);
    /* Set up custom network connection lost hook */
    SERVER_SET_HOOK(channel->server, connection_disconnected_hook, connection_disconnected, channel);

    /* Add a dedicated hardware control connection */
    channel->hw_connection = server_add_connection(channel->server, hw_host, hw_port);
    channel->hw_connection->is_active = TRUE;
    /* ..and a periodic timer to request its state */
    server_add_timer(channel->server, 0.1, 0, hw_timer_cb, channel);
    /* FIXME: temporary one */
    //server_connection_message(channel->hw_connection, "set_ii_power 1");

    /* Add a dedicated beholder connection */
    channel->beholder_connection = server_add_connection(channel->server, beholder_host, beholder_port);
    channel->beholder_connection->is_active = TRUE;

    /* Add a dedicated logger connection */
    channel->logger_connection = server_add_connection(channel->server, logger_host, logger_port);
    channel->logger_connection->is_active = TRUE;
    server_connection_message(channel->logger_connection, "iam id=channel%d", channel->id);

    /* Add a mount connection */
    channel->mount_connection = server_add_connection(channel->server, mount_host, mount_port);
    channel->mount_connection->is_active = TRUE;
    /* ..and a periodic timer to request its state */
    server_add_timer(channel->server, 1, 0, mount_timer_cb, channel);

    /* Set up some global variables to be used in subsystems */
    config_grabber_host = grabber_host;
    config_grabber_port = grabber_port;
    config_grabber_udp_port = udp_port;

    config_db_host = db_host;
    config_db_port = db_port;

    /* Query the DB for latest dark frame and flat */
    queue_add_with_destructor(channel->db_queue, CHANNEL_MSG_DB_FIND, make_string("dark"), free);
    queue_add_with_destructor(channel->db_queue, CHANNEL_MSG_DB_FIND, make_string("superflat"), free);

    channel->mask = image_create_from_fits(mask_filename);

    /* TODO: make configurable */
    channel_calibrate_celostates(channel, "celostate1.txt", "celostate2.txt");

    /* Launch subsystems in separate threads */
    pthread_create(&realtime_thread, NULL, realtime_worker, (void*)channel);
    pthread_create(&secondscale_thread, NULL, secondscale_worker, (void*)channel);
    pthread_create(&archive_thread, NULL, archive_worker, (void*)channel);
    pthread_create(&raw_thread, NULL, raw_worker, (void*)channel);
    pthread_create(&db_thread, NULL, db_worker, (void*)channel);
    pthread_create(&grabber_thread, NULL, grabber_worker, (void*)channel);

    channel_log(channel, "Channel started");

    /* Inform the script whether we should use II/second focus motor */
    script_set_boolean(channel->script, "has_ii", channel->has_ii);
    script_set_boolean(channel->script, "has_focus2", FALSE);

    if(!script_load(channel->script, "lua/channel.lua")){
        dprintf("Can't load channel script, exiting\n");
        return EXIT_FAILURE;
    }

    script_set_boolean(channel->script, "is_focused", channel->is_focused);
    /* Periodic timer to set some script variables */
    server_add_timer(channel->server, 1, 0, script_timer_cb, channel);

    image_jpeg_set_percentile(0.01, 0.999);

    /* Main cycle */
    while(!channel->is_quit){
        /* Check message queue for new messages and process if any. No timeouts here */
        process_queue_message(channel, queue_get(channel->server_queue));

        /* Wait for 1 ms for network events */
        server_cycle(channel->server, 1);

        /* Check for sent commands with expired timers */
        command_queue_check(channel->commands);
    }

    /* Wait for threads */
    pthread_join(realtime_thread, NULL);
    pthread_join(secondscale_thread, NULL);
    pthread_join(archive_thread, NULL);
    pthread_join(raw_thread, NULL);
    pthread_join(db_thread, NULL);

#ifdef __linux__
    {
        /* We will give 1-second timeout for grabber thread, and then kill it */
        struct timespec abstime;
        struct timeval tv;
        double timeout = 1.0;

        gettimeofday(&tv, NULL);

        abstime.tv_sec = tv.tv_sec + floor(timeout);
        abstime.tv_nsec = 1e3*tv.tv_usec + 1e9*(timeout - floor(timeout));

        if(pthread_timedjoin_np(grabber_thread, NULL, &abstime) != 0)
            pthread_kill(grabber_thread, SIGTERM);

        gettimeofday(&tv, NULL);

        /* Second try */
        abstime.tv_sec = tv.tv_sec + floor(timeout);
        abstime.tv_nsec = 1e3*tv.tv_usec + 1e9*(timeout - floor(timeout));

        if(pthread_timedjoin_np(grabber_thread, NULL, &abstime) != 0)
            pthread_kill(grabber_thread, SIGKILL);
    }
#else
    pthread_join(grabber_thread, NULL);
#endif

    channel_log(channel, "Channel finished");

    channel_delete(channel);

    return EXIT_SUCCESS;
}
