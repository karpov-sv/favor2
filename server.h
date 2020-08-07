#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>

#include "lists.h"
#include "time_str.h"

#define SERVER_DECLARE_HOOK(name, ...) \
    void (*name)(struct server_str *, ## __VA_ARGS__, void *); \
    void *name ## _data
#define SERVER_CALL_HOOK(server, name, ...) \
    if((server)->name) (server)->name((server), ## __VA_ARGS__, (server)->name ## _data)
#define SERVER_SET_HOOK(server, name, func, data) \
    do {(server)->name = (func); \
        (server)->name ## _data = (data);} while(0)

struct server_str;

typedef struct connection_str {
    LIST_HEAD(struct connection_str);

    char *host;
    int port;

    int is_active; /* Whether this connection will try to reestablish itself on disconnect */
    int is_waiting_for_restart; /* Temporary state variable */

    int is_connected;
    int is_finished;

    /* Received data */
    char *read_buffer;
    int read_length;

    /* Data to send */
    char *write_buffer;
    int write_length;

    /* Custom data processing callback - should return number of bytes processed */
    int (*data_read_hook)(struct server_str *, struct connection_str *, char *, int , void *);
    void *data_read_hook_data;

    /* Binary mode */
    int is_binary;
    int binary_length;
    /* Binary mode hook */
    void (*binary_read_hook)(struct server_str *, struct connection_str *, char *, int , void *);
    void *binary_read_hook_data;

    /* Mutex to prevent crash when using connection from several threads */
    pthread_mutex_t write_mutex;

    int socket; /* Actually interacts with clients */
} connection_str;

struct server_str;

typedef struct timer_str {
    LIST_HEAD(struct timer_str);

    /* User-supplied type of the timer */
    int type;

    /* Duration of the timer */
    double interval;

    /* Moment of timer expiration */
    time_str time;

    /* User-supplied callback */
    void (*callback)(struct server_str *, int , void *);
    /* User-supplied data - will not be freed() */
    void *callback_data;
} timer_str;

typedef struct server_str {
    int socket; /* Listens for incoming connections */

    /* Host and port to listen for */
    char *host;
    int port;

    /* Connections */
    struct list_head connections;

    /* Callbacks */
    SERVER_DECLARE_HOOK(line_read_hook, connection_str *, char *);
    SERVER_DECLARE_HOOK(client_connected_hook, connection_str *);
    SERVER_DECLARE_HOOK(connection_connected_hook, connection_str *);
    SERVER_DECLARE_HOOK(connection_disconnected_hook, connection_str *);

    /* User-supplied (e.g. UDP) socket and callbacks */
    int custom_socket;
    SERVER_DECLARE_HOOK(custom_socket_read_hook, int );
    SERVER_DECLARE_HOOK(custom_socket_write_hook, int );

    /* Timers */
    struct list_head timers;
} server_str;

server_str *server_create();
int server_listen(server_str *, char *, int );
void server_delete(server_str *);

connection_str *server_add_connection(server_str *, char *, int );
void server_connection_write_block(connection_str *, char *, int );
void server_connection_message(connection_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));
void server_connection_message_nozero(connection_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));
void server_broadcast_message(server_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));
void server_connection_cleanup(connection_str *);
void server_connection_close(connection_str *);
void server_connection_restart(server_str *, connection_str *);

void server_connection_wait_for_binary(connection_str *, int );

void server_cycle(server_str *, double );

/* Default hooks */
void server_default_line_read_hook(server_str *, connection_str *, char *, void *);
void server_default_client_connected_hook(server_str *, connection_str *, void *);
void server_default_connection_connected_hook(server_str *, connection_str *, void *);
void server_default_connection_disconnected_hook(server_str *, connection_str *, void *);

/* Timers interface */
timer_str *server_add_timer(server_str *, double , int, void (*)(server_str *, int , void *), void *);
void server_kill_timers_with_type(server_str *, int );
void server_kill_timers_with_data(server_str *, void *);
int server_number_of_timers_with_type(server_str *, int );

/* Auxiliary */
int server_has_connection(server_str *, connection_str *);

#endif /* SERVER_H */
