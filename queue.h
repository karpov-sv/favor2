#ifndef QUEUE_H
#define QUEUE_H

/* Generic message with a number and pointer to the data */
typedef struct {
    int event;
    void *data;

    /* User-configurable function to delete message data.  By default, free(),
       if non-NULL. Used only when the queue size is limited and we have to
       remove oldest elements to insert new ones */
    void (*data_delete)(void *);
} queue_message_str;

/* Generic thread-safe queue */
typedef struct {
    /* If zero, the queue is unlimited */
    int maximal_length;
    /* Current length */
    int length;

    /* Time to wait for if no messages are in queue. In seconds */
    double timeout;

    /* Mutex for data access protection */
    pthread_mutex_t mutex;
    /* Condition to wait for when queue is empty */
    pthread_cond_t nonempty;

    queue_message_str *messages;
} queue_str;

queue_str *queue_create(int );
void queue_delete(queue_str *);

void queue_clear(queue_str *);

void queue_add(queue_str *, int , void *);
void queue_add_with_destructor(queue_str *, int , void *, void (*)(void *));
queue_message_str queue_get(queue_str *);

void queue_wakeup(queue_str *);

int queue_length(queue_str *);

#endif /* QUEUE_H */
