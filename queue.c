#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "utils.h"

#include "queue.h"

queue_str *queue_create(int maximal_length)
{
    queue_str *queue = (queue_str *)malloc(sizeof(queue_str));

    queue->maximal_length = maximal_length;
    queue->length = 0;

    queue->timeout = 0.1;

    queue->messages = NULL;

    /* Parallel threads stuff */
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->nonempty, NULL);

    return queue;
}

void queue_clear(queue_str *queue)
{
    int d;

    if(!queue)
        return;

    pthread_mutex_lock(&queue->mutex);

    /* Delete each message data using user-supplied function */
    for(d = 0; d < queue->length; d++){
        if(queue->messages[d].data_delete && queue->messages[d].data)
            queue->messages[d].data_delete(queue->messages[d].data);
    }

    queue->length = 0;

    if(queue->messages)
        free(queue->messages);

    queue->messages = NULL;

    pthread_mutex_unlock(&queue->mutex);
}

void queue_delete(queue_str *queue)
{
    if(!queue)
        return;

    queue_clear(queue);

    free(queue);
}

/* Wakes up the clients waiting for a message */
void queue_wakeup(queue_str *queue)
{
    if(queue)
        pthread_cond_broadcast(&queue->nonempty);
}

void queue_shift(queue_str *queue)
{
    if(!queue->length)
        return;

    memmove(queue->messages, queue->messages + 1, sizeof(queue_message_str)*(queue->length - 1));

    queue->length --;

    queue->messages = realloc(queue->messages, sizeof(queue_message_str)*queue->length);
}

void queue_add(queue_str *queue, int event, void *data)
{
    queue_add_with_destructor(queue, event, data, free);
}

void queue_add_with_destructor(queue_str *queue, int event, void *data, void (*data_delete)(void *))
{
    if(!queue)
        return;

    pthread_mutex_lock(&queue->mutex);

    /* If queue is full, delete the oldest element and shift the queue */
    if(queue->maximal_length && queue->length >= queue->maximal_length){
        if(queue->messages[0].data_delete && queue->messages[0].data)
            queue->messages[0].data_delete(queue->messages[0].data);

        queue_shift(queue);
    }

    /* Add new message to the tail of queue */
    queue->messages = realloc(queue->messages, sizeof(queue_message_str)*(queue->length + 1));
    queue->messages[queue->length].event = event;
    queue->messages[queue->length].data = data;

    queue->messages[queue->length].data_delete = data_delete;

    queue->length ++;

    /* Wakeup the first client waiting for a message, if any */
    if(queue->length == 1)
        pthread_cond_signal(&queue->nonempty);

    pthread_mutex_unlock(&queue->mutex);
}

queue_message_str queue_get(queue_str *queue)
{
    queue_message_str message = {-1, NULL};

    if(!queue)
        return message;

    pthread_mutex_lock(&queue->mutex);

    if(!queue->length && queue->timeout > 0){
        /* The queue is empty. Let's wait for new messages, timeout or wakeup */
        struct timespec abstime;
        struct timeval tv;

        gettimeofday(&tv, NULL);

        abstime.tv_sec = tv.tv_sec + floor(queue->timeout);
        abstime.tv_nsec = 1e3*tv.tv_usec + 1e9*(queue->timeout - floor(queue->timeout));

        /* This will atomically release the mutex and wait for the nonempty
           condition or timeout. Mutex is locked again after. */
        pthread_cond_timedwait(&queue->nonempty, &queue->mutex, &abstime);
    }

    if(queue->length){
        /* Return the oldest message and shift the queue without deleting message content */
        message = queue->messages[0];
        queue_shift(queue);
    }

    pthread_mutex_unlock(&queue->mutex);

    return message;
}

int queue_length(queue_str *queue)
{
    int value = 0;

    if(queue){
        pthread_mutex_lock(&queue->mutex);

        value = queue->length;

        pthread_mutex_unlock(&queue->mutex);
    }

    return value;
}
