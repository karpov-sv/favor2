#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#undef DEBUG

#include "utils.h"
#include "command_queue.h"

command_queue_str *command_queue_create()
{
    command_queue_str *cq = (command_queue_str *)malloc(sizeof(command_queue_str));

    init_list(cq->commands_sent);

    cq->handler = NULL;
    cq->handler_data = NULL;

    cq->timeout_callback = NULL;
    cq->timeout_callback_data = NULL;

    return cq;
}

void command_queue_delete(command_queue_str *cq)
{
    command_sent_str *command = NULL;

    foreach(command, cq->commands_sent){
        free(command->name);
        if(command->prefix)
            free(command->prefix);
        del_from_list_in_foreach_and_run(command, free(command));
    }

    free_list(cq->commands_sent);

    free(cq);
}

static void command_handler_message(command_queue_str *cq, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    if(cq->handler)
        cq->handler(buffer, cq->handler_data);

    free(buffer);
}

void command_queue_add_with_prefix(command_queue_str *cq, connection_str *conn, const char *name, double timeout, char *prefix)
{
    command_sent_str *command = (command_sent_str *)malloc(sizeof(command_sent_str));

    dprintf("New command added to queue: %s\n", name);

    command->connection = conn;
    command->name = make_string("%s", name);
    command->prefix = prefix ? make_string("%s", prefix) : NULL;
    command->time = time_current();
    command->timeout = timeout;

    add_to_list_end(cq->commands_sent, command);
}

void command_queue_add(command_queue_str *cq, connection_str *conn, const char *name, double timeout)
{
    command_queue_add_with_prefix(cq, conn, name, timeout, NULL);
}

int command_queue_done(command_queue_str *cq, char *name)
{
    command_sent_str *command = NULL;
    int removed = 0;

    foreach(command, cq->commands_sent)
        if(!strcmp(command->name, name)){
            char *name = command->prefix ? make_string("%s___%s", command->prefix, command->name) : make_string("%s", command->name);

            if(command->connection)
                server_connection_message(command->connection, "%s_done", name);
            else
                command_handler_message(cq, "%s_done", name);

            removed ++;

            free(name);
            free(command->name);
            del_from_list_in_foreach_and_run(command, free(command));

            /* TODO: should we actually remove only the first occurrence of a command or all of them? */
            /* break; */
        }

    dprintf("%d command(s) reported as done and removed: %s\n", removed, name);

    return removed;
}

int command_queue_failure(command_queue_str *cq, char *name)
{
    command_sent_str *command = NULL;
    int removed = 0;

    foreach(command, cq->commands_sent)
        if(!strcmp(command->name, name)){
            char *name = command->prefix ? make_string("%s___%s", command->prefix, command->name) : make_string("%s", command->name);

            if(command->connection)
                server_connection_message(command->connection, "%s_failure", name);
            else
                command_handler_message(cq, "%s_failure", name);

            removed ++;

            free(name);
            free(command->name);
            if(command->prefix)
                free(command->prefix);
            del_from_list_in_foreach_and_run(command, free(command));

            /* TODO: should we actually remove only the first occurrence of a command or all of them? */
            /* break; */
        }

    dprintf("%d command(s) reported as failed and removed: %s\n", removed, name);

    return removed;
}

int command_queue_remove(command_queue_str *cq, char *name)
{
    command_sent_str *command = NULL;
    int removed = 0;

    foreach(command, cq->commands_sent)
        if(!strcmp(command->name, name)){
            removed ++;

            free(command->name);
            if(command->prefix)
                free(command->prefix);
            del_from_list_in_foreach_and_run(command, free(command));

            /* TODO: should we actually remove only the first occurrence of a command or all of them? */
            /* break; */
        }

    dprintf("%d command(s) removed: %s\n", removed, name);

    return removed;
}

int command_queue_remove_with_connection(command_queue_str *cq, connection_str *conn)
{
    command_sent_str *command = NULL;
    int removed = 0;

    dprintf("Removing commands for connection: %s:%d\n", conn->host, conn->port);

    foreach(command, cq->commands_sent)
        if(command->connection == conn){
            removed ++;
            free(command->name);
            if(command->prefix)
                free(command->prefix);
            del_from_list_in_foreach_and_run(command, free(command));
        }

    dprintf("%d command(s) removed belonging to connection %p\n", removed, conn);

    return removed;
}

int command_queue_contains(command_queue_str *cq, char *name)
{
    command_sent_str *command = NULL;
    int found = 0;

    foreach(command, cq->commands_sent)
        if(!strcmp(command->name, name)){
            found ++;

            break;
        }

    return found;
}

void command_queue_check(command_queue_str *cq)
{
    command_sent_str *command = NULL;
    time_str time = time_current();

    foreach(command, cq->commands_sent)
        if(time_interval(command->time, time) > 1e3*command->timeout){
            char *name = command->prefix ? make_string("%s___%s", command->prefix, command->name) : make_string("%s", command->name);

            /* Timeout expired */
            dprintf("Timeout expired for command: %s\n", command->name);

            if(command->connection)
                server_connection_message(command->connection, "%s_timeout", name);
            else
                command_handler_message(cq, "%s_timeout", name);

            if(cq->timeout_callback)
                cq->timeout_callback(command->name, cq->timeout_callback_data);

            free(name);
            free(command->name);
            if(command->prefix)
                free(command->prefix);
            del_from_list_in_foreach_and_run(command, free(command));
        }
}
