#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

#include "channel.h"
#include "image_udp.h"
#include "command.h"

typedef struct {
    channel_str *channel;

#ifdef ANDOR
    andor_str *andor;
#else
    server_str *server;
    connection_str *grabber_connection;
#endif

    double exposure; /* Rough exposure for various delay estimations */

    time_str start_time;

    int error_count;
    int is_initial_startup;
} grabber_str;

static void grabber_start(grabber_str *grabber)
{
    dprintf("Starting grabber acquisition\n");

    if(grabber->channel->grabber_state != STATE_ON){
#ifdef ANDOR
        andor_info(grabber->andor);
        andor_acquisition_start(grabber->andor);
#else
        server_connection_message(grabber->grabber_connection, "CONTROL NETSTART");
#endif
    }

    if(grabber->channel->grabber_state != STATE_ON)
        channel_log(grabber->channel, "Grabber started");

    pthread_mutex_lock(&grabber->channel->mutex);
    grabber->channel->grabber_state = STATE_STARTING;
    pthread_mutex_unlock(&grabber->channel->mutex);
    grabber->start_time = time_current();

#ifdef ANDOR
    if(grabber->andor->is_simcam){
        pthread_mutex_lock(&grabber->channel->mutex);
        grabber->channel->grabber_state = STATE_ERROR;
        pthread_mutex_unlock(&grabber->channel->mutex);
    }
#endif
}

static void grabber_stop(grabber_str *grabber)
{
    dprintf("Stopping grabber acquisition\n");

    if(grabber->channel->grabber_state != STATE_OFF){
#ifdef ANDOR
        andor_acquisition_stop(grabber->andor);
#else
        server_connection_message(grabber->grabber_connection, "CONTROL NETSTOP");
#endif
    }

    if(grabber->channel->grabber_state != STATE_OFF)
        channel_log(grabber->channel, "Grabber stopped");
    pthread_mutex_lock(&grabber->channel->mutex);
    grabber->channel->grabber_state = STATE_OFF;
    pthread_mutex_unlock(&grabber->channel->mutex);

#ifdef ANDOR
    if(grabber->andor->is_simcam){
        pthread_mutex_lock(&grabber->channel->mutex);
        grabber->channel->grabber_state = STATE_ERROR;
        pthread_mutex_unlock(&grabber->channel->mutex);
    }
#endif
}

#ifndef ANDOR
void process_udp_raw_data(struct frame_head *head, void *ptr)
{
    grabber_str *grabber = (grabber_str *)ptr;
    char *data = (char *)head + head->head_size;//sizeof(struct frame_head);
    image_str *image = image_create_from_data(head, data);

    /* Filter out broken frames - FIXME! */
    if(grabber->channel->hw_ii_power == 1 && (grabber->channel->hw_cover == 1 || grabber->channel->hw_lamp == 1) && image_mean(image) < 180 && grabber->channel->state != CHANNEL_STATE_AUTOFOCUS){
        image_delete(image);
        free(head);
        return;
    }

    if(grabber->channel->grabber_state == STATE_STARTING){
        pthread_mutex_lock(&grabber->channel->mutex);
        grabber->channel->grabber_state = STATE_ON;
        pthread_mutex_unlock(&grabber->channel->mutex);
    }

    /* Unpack and dispatch the image */
    queue_add_with_destructor(grabber->channel->server_queue, CHANNEL_MSG_IMAGE, image, (void (*)(void *))image_delete);
    free(head);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    grabber_str *grabber = (grabber_str *)data;

    if(connection == grabber->grabber_connection){
        server_connection_message(connection, "CONTROL NETSTART");
    }

    server_default_connection_connected_hook(server, connection, NULL);
}
#endif

void process_grabber_command(grabber_str *grabber, char *string)
{
    command_str *command = command_parse(string);

    if(command_match(command, "set_grabber")){
#ifdef ANDOR
#ifndef ANDOR_FAKE
        double exposure = -1;
        int trigger = -1;
        int binning = -1;
        int rate = -1;
        int shutter = -1;
        int preamp = -1;
        int filter = -1;
        int overlap = -1;
        int count = -1;

        int is_started = grabber->channel->grabber_state == STATE_ON;

        command_args(command,
                     "exposure=%lf", &exposure,
                     "trigger=%d", &trigger,
                     "binning=%d", &binning,
                     "rate=%d", &rate,
                     "shutter=%d", &shutter,
                     "preamp=%d", &preamp,
                     "filter=%d", &filter,
                     "overlap=%d", &overlap,
                     "count=%d", &count,
                     NULL);

        if(is_started)
            grabber_stop(grabber);

        if(trigger >= 0)
            AT_SetEnumIndex(grabber->andor->handle, L"TriggerMode", trigger);

        if(exposure > 0){
            grabber->exposure = exposure;
            if(exposure == 0.1 /* && get_enum_index(grabber->andor->handle, "ElectronicShutteringMode") == 1 */)
                /* Adjust the exposure so External trigger in Global shutter will still give 10 fps */
                exposure = 0.0995;
            AT_SetFloat(grabber->andor->handle, L"ExposureTime", exposure);
        }
        if(binning >= 0)
            AT_SetEnumIndex(grabber->andor->handle, L"AOIBinning", binning);
        if(rate >= 0)
            AT_SetEnumIndex(grabber->andor->handle, L"PixelReadoutRate", rate);
        if(shutter >= 0)
            AT_SetEnumIndex(grabber->andor->handle, L"ElectronicShutteringMode", shutter);
        if(preamp >= 0){
            AT_SetEnumIndex(grabber->andor->handle, L"SimplePreAmpGainControl", preamp);
            if(preamp < 2)
                AT_SetEnumString(grabber->andor->handle, L"PixelEncoding", L"Mono12");
        }
        if(filter >= 0)
            AT_SetBool(grabber->andor->handle, L"SpuriousNoiseFilter", filter);
        if(overlap >= 0)
            AT_SetBool(grabber->andor->handle, L"Overlap", overlap);

        if(count > 0){
            AT_SetEnumString(grabber->andor->handle, L"CycleMode", L"Fixed");
            AT_SetInt(grabber->andor->handle, L"FrameCount", count);
        } else {
            AT_SetEnumString(grabber->andor->handle, L"CycleMode", L"Continuous");
        }

        if(is_started)
            grabber_start(grabber);
#endif
#endif
    }

    command_delete(command);
}

void *grabber_worker(void *data)
{
    channel_str *channel = (channel_str *)data;
    grabber_str *grabber = (grabber_str *)malloc(sizeof(grabber_str));
    image_udp_str *udp = NULL;
    int is_quit = FALSE;
    int is_first = TRUE;

    grabber->channel = channel;

#ifdef ANDOR
    grabber->andor = andor_create();
    grabber->exposure = 0.1; /* Hard-coded initial value */
#ifndef ANDOR_FAKE
    AT_SetEnumString(grabber->andor->handle, L"TriggerMode", L"External");
    AT_SetEnumIndex(grabber->andor->handle, L"ElectronicShutteringMode", 1);
    AT_SetFloat(grabber->andor->handle, L"ExposureTime", 0.0995);
#endif

#else
    grabber->server = server_create();

    /* Set up custom network post-connection code */
    SERVER_SET_HOOK(grabber->server, connection_connected_hook, connection_connected, grabber);

    /* Dedicated grabber connection */
    grabber->grabber_connection = server_add_connection(grabber->server, config_grabber_host, config_grabber_port);
    grabber->grabber_connection->is_active = TRUE;

    /* Attach UDP socket and set up corresponding callback */
    udp = image_udp_attach(grabber->server, config_grabber_udp_port);
    udp->callback_raw = process_udp_raw_data;
    udp->callback_raw_data = grabber;

    grabber->exposure = 0.13;
#endif /* ANDOR */

    grabber->error_count = 0;
    grabber->is_initial_startup = TRUE;

    grabber_start(grabber);

    dprintf("Grabber subsystem started\n");

    while(!is_quit){
        queue_message_str m = queue_get(channel->grabber_queue);

        pthread_mutex_lock(&channel->mutex);
        /* It is being set in main thread */
        time_str last_image_time = channel->last_image_time;
        pthread_mutex_unlock(&channel->mutex);

        double time_since_last_image = 1e-3*time_interval(last_image_time, time_current());
        double time_since_grabber_start = 1e-3*time_interval(grabber->start_time, time_current());
        double time_limit = (grabber->exposure < 3) ? 10 : 3*grabber->exposure;

#ifdef ANDOR
        image_str *image = andor_wait_image(grabber->andor, 0.001);

        if(image /* && !is_first */){
            queue_add_with_destructor(channel->server_queue, CHANNEL_MSG_IMAGE, image, (void (*)(void *))image_delete);

            pthread_mutex_lock(&channel->mutex);

#ifndef ANDOR_FAKE
            channel->andor_temperature = get_float(grabber->andor->handle, L"SensorTemperature");
            channel->andor_temperaturestatus = get_enum_index(grabber->andor->handle, L"TemperatureStatus");
            channel->andor_exposure = get_float(grabber->andor->handle, L"ExposureTime");
            channel->andor_framerate = get_float(grabber->andor->handle, L"FrameRate");
            channel->andor_binning = get_enum_index(grabber->andor->handle, L"AOIBinning");
            channel->andor_filter = get_bool(grabber->andor->handle, L"SpuriousNoiseFilter");
            channel->andor_shutter = get_enum_index(grabber->andor->handle, L"ElectronicShutteringMode");
            channel->andor_preamp = get_enum_index(grabber->andor->handle, L"SimplePreAmpGainControl");
#endif /* ANDOR_FAKE */

            if(!grabber->andor->is_simcam && channel->grabber_state == STATE_STARTING)
                channel->grabber_state = STATE_ON;

            pthread_mutex_unlock(&channel->mutex);

            /* Reset error counter */
            grabber->error_count = 0;
            grabber->is_initial_startup = FALSE;
        } else if(image){
            /* Get rid of the first image as it is corrupted in global shutter */
            is_first = FALSE;
            image_delete(image);
        }

#ifdef ANDOR
        if(grabber->andor->is_simcam){
            pthread_mutex_lock(&channel->mutex);
            channel->grabber_state = STATE_ERROR;
            pthread_mutex_unlock(&channel->mutex);
        }
#endif

#else
        server_cycle(grabber->server, 10);
#endif

#ifndef ANDOR_FAKE
        /* Signal error condition if no frames are received in 10 seconds */
        if(channel->grabber_state == STATE_ON &&
           !time_is_zero(last_image_time) &&
           time_since_last_image > time_limit){
            //grabber->channel->grabber_state = STATE_ERROR;
            dprintf("Error in ON state: %g s since last image, %g allowed\n", time_since_last_image, time_limit);

            /* Restart the grabber */
            grabber_stop(grabber);
            grabber_start(grabber);
        }

        if(channel->grabber_state == STATE_STARTING &&
           time_since_grabber_start > time_limit){
            dprintf("Error in STARTING state: %g s since start, %g allowed\n", time_since_grabber_start, time_limit);

            if(grabber->error_count >= 3 || grabber->is_initial_startup){
                pthread_mutex_lock(&channel->mutex);
                channel->grabber_state = STATE_ERROR;
                pthread_mutex_unlock(&channel->mutex);
            } else
                grabber->error_count ++;
        }
#endif

        /* Lift error state if frames are coming to us againg */
        if(channel->grabber_state == STATE_ERROR &&
           !time_is_zero(last_image_time) &&
           time_since_last_image < time_limit
#ifdef ANDOR
           && !grabber->andor->is_simcam
#endif
           ){
            pthread_mutex_lock(&channel->mutex);
            channel->grabber_state = STATE_ON;
            pthread_mutex_unlock(&channel->mutex);
        }

        switch(m.event){
        case CHANNEL_MSG_GRABBER_START:
            grabber_start(grabber);
#ifdef ANDOR
            is_first = TRUE;
#endif
            queue_add(channel->server_queue, CHANNEL_MSG_GRABBER_STARTED, NULL);
            break;

        case CHANNEL_MSG_GRABBER_STOP:
            grabber_stop(grabber);
            queue_add(channel->server_queue, CHANNEL_MSG_GRABBER_STOPPED, NULL);
            break;

        case CHANNEL_MSG_COMMAND:
            process_grabber_command(grabber, (char *)m.data);
            queue_add(channel->server_queue, CHANNEL_MSG_COMMAND_DONE, make_string("grabber"));
            free(m.data);
            break;

        case CHANNEL_MSG_EXIT:
            is_quit = TRUE;
            break;
        }
    }

    /* It will probably hangs if Andor power is turned off */
    //grabber_stop(grabber);

#ifdef ANDOR
    /* It will also most probably hangs if Andor power is turned off */
    //andor_delete(grabber->andor);
#else
    /* Detach UDP socket and free unfinished data */
    image_udp_detach(grabber->server, udp);

    server_delete(grabber->server);
#endif

    dprintf("Grabber subsystem finished\n");

    return NULL;
}
