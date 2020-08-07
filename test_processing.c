#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "utils.h"

#include "command.h"
#include "channel.h"
#include "simulator.h"

#ifdef PROFILE_PERFORMANCE
#include <sys/time.h>
static double timer_t0 = 0;
static double timer_t1 = 0;

static inline double current_time(void) {
   struct timeval t;
   gettimeofday(&t,NULL);
   return (double) t.tv_sec + t.tv_usec/1000000.0;
}
#endif /* PROFILE_PERFORMANCE */

channel_str *channel_create(int port)
{
    channel_str *channel = (channel_str *)malloc(sizeof(channel_str));

    channel->server = NULL;

    channel->server_queue = queue_create(0);
    /* server thread will not wait for messages as it has to operate together
       with select() loop */
    channel->server_queue->timeout = 0;

    channel->realtime_queue = queue_create(100);
    channel->realtime_queue->timeout = 1;

    channel->secondscale_queue = queue_create(100);
    channel->secondscale_queue->timeout = 1;

    channel->raw_queue = queue_create(100);
    channel->raw_queue->timeout = 1;

    channel->db_queue = NULL;

    return channel;
}

void channel_delete(channel_str *channel)
{
    if(!channel)
        return;

    queue_delete(channel->server_queue);
    queue_delete(channel->realtime_queue);
    queue_delete(channel->secondscale_queue);
    queue_delete(channel->raw_queue);

    free(channel);
}

void channel_store_image(channel_str *channel, image_str *image, char *type)
{
}

void channel_log(channel_str *channel, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    free(buffer);
}

static void process_queue_message(channel_str *channel, queue_message_str m)
{
    switch(m.event){
        /* Toy events */
    case CHANNEL_MSG_PROCESSED_IMAGE:
        queue_add_with_destructor(channel->raw_queue, CHANNEL_MSG_IMAGE, m.data, (void (*)(void *))image_delete);
        /* No messages received */
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    channel_str *channel = NULL;
    pthread_t realtime_thread;
    pthread_t secondscale_thread;
    pthread_t raw_thread;

    simulator_str *sim = NULL;
    int width = 2560;
    int height = 2160;
    double ra0 = 56.75;
    double dec0 = 24.11;
    double pixscale = 25.9;

    parse_args(argc, argv,
               "width=%d", &width,
               "height=%d", &height,
               "ra0=%lf", &ra0,
               "dec0=%lf", &dec0,
               "pixscale=%lf", &pixscale,
               NULL);

    channel = channel_create(0);

    channel->ra0 = ra0;
    channel->dec0 = dec0;
    channel->pixscale = 25.0;

    channel->rawpath = "IMG";

    sim = simulator_create(width, height);
    simulator_set_field(sim, ra0, dec0, pixscale);

    sim->satellite_probability = 0.05*0;
    sim->meteor_probability = 0.05*0;
    sim->flash_probability = 0.05*0;

    /* Launch subsystems in separate threads */
    pthread_create(&realtime_thread, NULL, realtime_worker, (void*)channel);
    pthread_create(&secondscale_thread, NULL, secondscale_worker, (void*)channel);
    pthread_create(&raw_thread, NULL, raw_worker, (void*)channel);

    /* Main cycle */
    while(sim->frame_number < 2000){
        image_str *image = simulator_get_image(sim);

#ifdef PROFILE_PERFORMANCE
        timer_t1 = current_time();
        printf("simulator %g\n", timer_t1 - timer_t0);
        timer_t0 = timer_t1;
#endif /* PROFILE_PERFORMANCE */

        /* Check message queue for new messages and process if any. No timeouts here */
        process_queue_message(channel, queue_get(channel->server_queue));

        dprintf("Sending frame %Ld\n", sim->frame_number);

        /* /\* Copy of image to data writer *\/ */
        /* queue_add_with_destructor(channel->raw_queue, CHANNEL_MSG_IMAGE, */
        /*                           image_copy(image), (void (*)(void *))image_delete); */

        /* and original image to realtime processing */
        queue_add_with_destructor(channel->realtime_queue, CHANNEL_MSG_IMAGE,
                                  image, (void (*)(void *))image_delete);


        simulator_iterate(sim);
    }

    /* Send finalize signal */
    queue_add(channel->realtime_queue, CHANNEL_MSG_EXIT, NULL);
    queue_add(channel->secondscale_queue, CHANNEL_MSG_EXIT, NULL);
    queue_add(channel->raw_queue, CHANNEL_MSG_EXIT, NULL);

    /* Wait for threads */
    pthread_join(realtime_thread, NULL);
    pthread_join(secondscale_thread, NULL);
    pthread_join(raw_thread, NULL);

    simulator_delete(sim);

    channel_delete(channel);

    return EXIT_SUCCESS;
}
