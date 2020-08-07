#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "time_str.h"
#include "server.h"

/* Structure to hold data on commands sent and awaiting reply */
typedef struct command_sent_str {
    LIST_HEAD(struct command_sent_str);

    /* Connection to send messages to. If NULL, messages will be sent to
       globally registered message handler instead. */
    connection_str *connection;

    /* Name of the command */
    char *name;
    /* Time of the command issue */
    time_str time;
    /* Timeout to wait for the command results */
    double timeout;
    /* Optinlal target prefix - will be appended to timeout messages etc */
    char *prefix;
} command_sent_str;

/* Structure to handle the list of commands awaiting reply and issue error
   messages if no reply received in a given time */
typedef struct {
    struct list_head commands_sent;

    /* Handler for messages when connection is NULL */
    void (*handler)(char *, void *);
    void *handler_data;

    /* Optional callback for 'timeout' situation, will be called after sending
       the timeout message with command name as an argument */
    void (*timeout_callback)(char *, void *);
    void *timeout_callback_data;
} command_queue_str;

command_queue_str *command_queue_create();
void command_queue_delete(command_queue_str *);

/* Register new command we just sent */
void command_queue_add(command_queue_str *, connection_str *, const char *, double );
void command_queue_add_with_prefix(command_queue_str *, connection_str *, const char *, double , char *);
/* The command finished ok - report command_done and remove it.  Will return
   the number of commands removed. */
int command_queue_done(command_queue_str *, char *);
/* The command failed - report command_failure and remove it.  Will return
   the number of commands removed. */
int command_queue_failure(command_queue_str *, char *);
/* Silently remove the command and return the number of commands removed */
int command_queue_remove(command_queue_str *, char *);
int command_queue_remove_with_connection(command_queue_str *, connection_str *);
/* Check whether there are commands with given name in queue */
int command_queue_contains(command_queue_str *, char *);
/* Check timeouts, should be called periodically */
void command_queue_check(command_queue_str *);

#endif /* COMMAND_QUEUE_H */
