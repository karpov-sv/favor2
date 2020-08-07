#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <math.h>
#include <errno.h>

#include "utils.h"
#include "lists.h"
#include "time_str.h"

#include "server.h"

static int init_sockaddr(struct sockaddr_in *name, const char *hostname, u_int16_t port)
{
    struct hostent *hostinfo;

    name->sin_family = AF_INET;
    name->sin_port = htons(port);

    /* FIXME: implement non-blocking name resolving */

    /* Hostname lookup */
    hostinfo = gethostbyname(hostname);

    /* IP-address lookup */
    if(!hostinfo)
        hostinfo = gethostbyaddr(hostname, strlen(hostname), AF_INET);

    /* Lookup failure */
    if(!hostinfo)
        return return_with_error("hostname");

    name->sin_addr = *(struct in_addr *) hostinfo->h_addr;

    return 1;
}

static int create_socket(char *hostname, int port, int is_server)
{
    struct sockaddr_in name;
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    unsigned int opt = 1;

    if(sock < 0)
        return return_with_error("socket");

    /* We wish completely non-blocking I/O */
    fcntl(sock, F_SETFL, O_NONBLOCK);
    /* We wish the data to be sent as soon as possible */
    setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &opt, sizeof(opt));
    /* It is better to turn on TCP built-in keepalive protocol.  We set up
       sending keepalive messages each 10 seconds, with 3 retries 1 second long
       each if no responce is received. This is per-socket equivalent to

       echo 1 > /proc/sys/net/ipv4/tcp_keepalive_intvl
       echo 3 > /proc/sys/net/ipv4/tcp_keepalive_probes
       echo 10 > /proc/sys/net/ipv4/tcp_keepalive_time

       TODO: implement protocol-based KEEPALIVE
    */
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    /* FIXME: not working on MacOSX */
#ifdef __linux__
    opt = 1;
    setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, &opt, sizeof(opt));
    opt = 3;
    setsockopt(sock, SOL_TCP, TCP_KEEPCNT, &opt, sizeof(opt));
    opt = 10;
    setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &opt, sizeof(opt));
#elif __CYGWIN__
    /* Cygwin stuff */
#else
    /* OSX stuff - disable SIGPIPEs */
    opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif

    if(is_server){
        /* Server-side socket */
        name.sin_family = AF_INET;
        name.sin_port = htons(port);
        name.sin_addr.s_addr = htonl(INADDR_ANY);

        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(sock, (struct sockaddr *) &name, sizeof (name)) < 0){
            close(sock);
            return return_with_error("bind");
        }
    } else {
        /* Client-side socket */
        int res = init_sockaddr(&name, hostname, port);

        if(res < 0){
            close(sock);
            return return_with_error("connect");
        }

        res = connect(sock, (struct sockaddr *) &name, sizeof (name));

        if(res < 0 && errno != EINPROGRESS){
            close(sock);
            return return_with_error("connect");
        } else if(res >= 0){
            dprintf("async connect() connected - this should not be!\n");
            exit(1);
        }
    }

    return sock;
}

void server_default_line_read_hook(server_str *server, connection_str *connection, char *line, void *data)
{
    dprintf("Line received from %s:%d: %s\n", connection->host, connection->port, line);
}

void server_default_client_connected_hook(server_str *server, connection_str *connection, void *data)
{
    dprintf("server: accepted connection from %s:%d at %s\n",
            connection->host, connection->port, timestamp(time_current()));
}

void server_default_connection_connected_hook(server_str *server, connection_str *connection, void *data)
{
    dprintf("server: connected to %s:%d at %s\n",
            connection->host, connection->port, timestamp(time_current()));
}

void server_default_connection_disconnected_hook(server_str *server, connection_str *connection, void *data)
{
    dprintf("server: disconnected from %s:%d at %s\n",
            connection->host, connection->port, timestamp(time_current()));
}

server_str *server_create(char *host, int port)
{
    server_str *server = (server_str *)malloc(sizeof(server_str));

    server->socket = -1;
    server->host = NULL;

    init_list(server->connections);

    SERVER_SET_HOOK(server, line_read_hook, server_default_line_read_hook, NULL);
    SERVER_SET_HOOK(server, client_connected_hook, server_default_client_connected_hook, NULL);
    SERVER_SET_HOOK(server, connection_connected_hook, server_default_connection_connected_hook, NULL);
    SERVER_SET_HOOK(server, connection_disconnected_hook, server_default_connection_disconnected_hook, NULL);

    server->custom_socket = -1;
    SERVER_SET_HOOK(server, custom_socket_read_hook, NULL, NULL);
    SERVER_SET_HOOK(server, custom_socket_write_hook, NULL, NULL);

    init_list(server->timers);

    /* Disable SIGPIPEs */
    signal(SIGPIPE, SIG_IGN);

    return server;
}

int server_listen(server_str *server, char *host, int port)
{
    int socket = create_socket(host, port, TRUE);

    dprintf("Listening for incoming connections on %s:%d\n", host, port);

    server->socket = -1;

    if(socket >= 0 && listen(socket, 1) >= 0){
        server->socket = socket;

        server->host = make_string("%s", host);
        server->port = port;
    }

    return server->socket;
}

connection_str *server_connection_create(int sock, const char *host, int port)
{
    connection_str *connection = (connection_str *)malloc(sizeof(connection_str ));

    connection->socket = sock;

    connection->host = make_string("%s", host);
    connection->port = port;

    connection->read_buffer = NULL;
    connection->read_length = 0;

    connection->write_buffer = NULL;
    connection->write_length = 0;

    connection->is_connected = FALSE;
    connection->is_finished = FALSE;
    connection->is_active = FALSE;
    connection->is_waiting_for_restart = FALSE;

    connection->data_read_hook = NULL;
    connection->data_read_hook_data = NULL;

    connection->is_binary = FALSE;
    connection->binary_length = 0;
    connection->binary_read_hook = NULL;
    connection->binary_read_hook_data = NULL;

    pthread_mutex_init(&connection->write_mutex, NULL);

    return connection;
}

void server_connection_cleanup(connection_str *connection)
{
    connection->read_buffer = realloc(connection->read_buffer, 0);
    connection->read_length = 0;

    pthread_mutex_lock(&connection->write_mutex);
    connection->write_buffer = realloc(connection->write_buffer, 0);
    connection->write_length = 0;
    pthread_mutex_unlock(&connection->write_mutex);

    if(connection->socket >= 0)
        close(connection->socket);

    connection->socket = -1;

    connection->is_binary = FALSE;
    connection->binary_length = 0;

    connection->is_connected = FALSE;
}

void server_connection_delete(connection_str *connection)
{
    server_connection_cleanup(connection);

    free(connection->host);

    /* FIXME: check whether the connection awaits restart! */

    pthread_mutex_destroy(&connection->write_mutex);

    free(connection);
}

void server_delete(server_str *server)
{
    connection_str *connection;

    if(!server)
        return;

    foreach(connection, server->connections){
        del_from_list_in_foreach_and_run(connection, server_connection_delete(connection));
    }

    free_list(server->connections);

    free_list(server->timers);

    close(server->socket);

    free(server);
}

/* Add an outgoing (i.e. client) connection to the pool */
connection_str *server_add_connection(server_str *server, char *host, int port)
{
    int sock = create_socket(host, port, FALSE);
    connection_str *connection = server_connection_create(sock, host, port);

    /* On successful async connection, we'll receive a message later */
    connection->is_connected = FALSE;

    add_to_list(server->connections, connection);

    dprintf("Connection to %s:%d registered at socket %d\n", host, port, connection->socket);

    return connection;
}

void server_connection_write(connection_str *connection)
{
    pthread_mutex_lock(&connection->write_mutex);

    if(!connection->write_length || connection->socket < 0 || !connection->is_connected){
        /* */
    } else while(connection->write_length){

#ifdef __linux__
            int chunk = send(connection->socket, connection->write_buffer, connection->write_length, MSG_NOSIGNAL);
#else
            int chunk = write(connection->socket, connection->write_buffer, connection->write_length);
#endif

            if(chunk <= 0)
                break;
            else {
                memmove(connection->write_buffer, connection->write_buffer + chunk, connection->write_length - chunk);
                connection->write_buffer = realloc(connection->write_buffer, connection->write_length - chunk);
                connection->write_length -= chunk;
            }
        }

    pthread_mutex_unlock(&connection->write_mutex);
}

void server_connection_write_block(connection_str *connection, char *buffer, int length)
{
    pthread_mutex_lock(&connection->write_mutex);

    if(length <= 0 || connection->socket < 0){
        pthread_mutex_unlock(&connection->write_mutex);
        return;
    }

    connection->write_buffer = realloc(connection->write_buffer, connection->write_length + length);
    memcpy(connection->write_buffer + connection->write_length, buffer, length);
    connection->write_length += length;

    pthread_mutex_unlock(&connection->write_mutex);

    server_connection_write(connection);
}

void server_connection_message(connection_str *connection, const char *template, ...)
{
    va_list ap;
    char *buffer = NULL;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    if(connection)
        server_connection_write_block(connection, buffer, strlen(buffer) + 1);

    free(buffer);
}

void server_connection_message_nozero(connection_str *connection, const char *template, ...)
{
    va_list ap;
    char *buffer = NULL;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    if(connection)
        server_connection_write_block(connection, buffer, strlen(buffer));

    free(buffer);
}

void server_broadcast_message(server_str *server, const char *template, ...)
{
    connection_str *conn;
    va_list ap;
    char *buffer;

    va_start (ap, template);
    vasprintf ((char**)&buffer, template, ap);
    va_end (ap);

    foreach(conn, server->connections)
        server_connection_write_block(conn, buffer, strlen(buffer) + 1);

    free(buffer);
}


/* Add an incoming connection to the pool */
void server_accept(server_str *server)
{
    struct sockaddr_in clientaddr;
    unsigned int size = sizeof(clientaddr);
    int sock = accept(server->socket, (struct sockaddr *) &clientaddr, &size);
    const char *host = inet_ntoa(clientaddr.sin_addr);
    int port = clientaddr.sin_port;
    connection_str *connection = server_connection_create(sock, host, port);

    /* We wish completely non-blocking I/O */
    fcntl(sock, F_SETFL, O_NONBLOCK);

    connection->is_connected = TRUE;

    SERVER_CALL_HOOK(server, client_connected_hook, connection);

    add_to_list(server->connections, connection);
}

void server_restart_connection(server_str *server, connection_str *connection)
{
    if(connection->socket >= 0)
        close(connection->socket);

    connection->socket = create_socket(connection->host, connection->port, FALSE);
}

static void restart_connection_callback(server_str *server, int type, void *ptr)
{
    connection_str *connection = (connection_str *)ptr;

    connection->is_waiting_for_restart = FALSE;

    server_restart_connection(server, connection);
}

void server_close_connection(server_str *server, connection_str *connection)
{
    del_from_list(connection);

    SERVER_CALL_HOOK(server, connection_disconnected_hook, connection);

    if(connection->host)
        free(connection->host);

    server_connection_cleanup(connection);

    free(connection);
}

void server_connection_wait_for_binary(connection_str *connection, int length)
{
    connection->is_binary = TRUE;
    connection->binary_length = length;
}

void server_connection_close(connection_str *connection)
{
    connection->is_finished = TRUE;
}

void server_connection_restart(server_str *server, connection_str *connection)
{
    /* Reset all connection data as it may be invalid at this point */
    server_connection_cleanup(connection);
    /* Try to reestablish the connection */
    server_add_timer(server, 1, 0, restart_connection_callback, connection);
    connection->is_waiting_for_restart = TRUE;
}

void server_connection_read(server_str *server, connection_str *connection)
{
    int bytes_read = 0;
    int start = 0;
    int end = -1;
    int pos = 0;

    /* Read data to the connection buffer */
    while(1){
        int max_chunk_size = 1024;
        int chunk;

        connection->read_buffer = realloc(connection->read_buffer, connection->read_length + bytes_read + max_chunk_size);
        chunk = read(connection->socket, connection->read_buffer + connection->read_length, max_chunk_size);

        /* EOF ? */
        if(chunk <= 0)
            break;

        bytes_read += chunk;
        connection->read_length += chunk;

        /* Got all available data ? */
        if(chunk < max_chunk_size)
            break;
    }

    if(!bytes_read){
        /* End-of-stream */

        if(connection->read_length && !connection->is_binary){
            /* Connection still has textual data, and we may try to use it */
            connection->read_buffer = realloc(connection->read_buffer, connection->read_length + 1);
            connection->read_buffer[connection->read_length] = '\0';
            SERVER_CALL_HOOK(server, line_read_hook, connection, connection->read_buffer);
        }

        if(connection->is_active && !connection->is_waiting_for_restart){
            dprintf("Lost connection to %s:%d, initiating restart\n", connection->host, connection->port);

            server_connection_restart(server, connection);
        } else
            server_close_connection(server, connection);
    } else {
        if(connection->data_read_hook){
            /* Process the data using custom hook */
            int processed = 0;
            int chunk = 0;

            /* Call the hook repeatedly until no data is processed */
            while(processed < connection->read_length &&
                  (chunk = connection->data_read_hook(server, connection, connection->read_buffer + processed, connection->read_length - processed, connection->data_read_hook_data)) != 0){

                processed += chunk;
            }

            if(processed > 0){
                memmove(connection->read_buffer, connection->read_buffer + processed, connection->read_length - processed);
                connection->read_length -= processed;
                connection->read_buffer = realloc(connection->read_buffer, connection->read_length);
            }
        } else if(!connection->is_binary){
            /* Parse and find NULL-terminated lines */
            while(pos < connection->read_length && !connection->is_binary && !connection->is_finished){
                if(connection->read_buffer[pos] == '\0' ||
                   connection->read_buffer[pos] == '\r' ||
                   connection->read_buffer[pos] == '\n'){
                    if(pos - start >= 0){
                        /* Command found */
                        connection->read_buffer[pos] = '\0';

                        if(strlen(connection->read_buffer + start) > 0)
                            SERVER_CALL_HOOK(server, line_read_hook, connection, connection->read_buffer + start);
                        end = pos;
                    }

                    start = pos + 1;
                }

                pos ++;
            }

            if(end >= 0){
                memmove(connection->read_buffer, connection->read_buffer + end + 1, connection->read_length - end - 1);
                connection->read_length -= end + 1;
                connection->read_buffer = realloc(connection->read_buffer, connection->read_length);
            }
        }

        if(connection->is_binary) {
            /* Binary mode of connection */
            if(connection->read_length >= connection->binary_length) {
                /* All the binary data read */
                if(connection->binary_read_hook)
                    connection->binary_read_hook(server, connection, connection->read_buffer, connection->binary_length, connection->binary_read_hook_data);
                else
                    dprintf("Read %d bytes of binary data; unhandled\n", connection->binary_length);

                memmove(connection->read_buffer, connection->read_buffer + connection->binary_length, connection->read_length - connection->binary_length);
                connection->read_length -= connection->binary_length;
                connection->read_buffer = realloc(connection->read_buffer, connection->read_length);

                connection->is_binary = FALSE;
                connection->binary_length = 0;
            }
        }

        if(connection->is_finished)
            server_close_connection(server, connection);
    }
}

timer_str *server_add_timer(server_str *server, double interval, int type,
                            void (*callback)(server_str *, int , void *), void *callback_data)
{
    timer_str *timer = (timer_str *)malloc(sizeof(timer_str));
    time_str time = time_current();

    time_increment(&time, interval);

    timer->type = type;
    timer->time = time;
    timer->interval = interval;

    timer->callback = callback;
    timer->callback_data = callback_data;

    add_to_list(server->timers, timer);

    return timer;
}

void server_kill_timers_with_type(server_str *server, int type)
{
    timer_str *timer = NULL;

    foreach(timer, server->timers){
        if(timer->type == type)
            del_from_list_in_foreach_and_run(timer, free(timer));
    }
}

void server_kill_timers_with_data(server_str *server, void *data)
{
    timer_str *timer = NULL;

    foreach(timer, server->timers){
        if(timer->callback_data == data)
            del_from_list_in_foreach_and_run(timer, free(timer));
    }
}

int server_number_of_timers_with_type(server_str *server, int type)
{
    timer_str *timer = NULL;
    int number = 0;

    foreach(timer, server->timers){
        if(timer->type == type)
            number ++;
    }

    return number;
}

void server_check_timers(server_str *server)
{
    timer_str *timer = NULL;
    time_str time = time_current();

    foreach(timer, server->timers){
        /* Whether the timer expired */
        if(time_interval(time, timer->time) < 0){
            if(timer->callback)
                timer->callback(server, timer->type, timer->callback_data);

            del_from_list_in_foreach_and_run(timer, free(timer));
        }
    }
}

void server_cycle(server_str *server, double wait)
{
    connection_str *connection;
    fd_set rset;
    fd_set wset;
    struct timeval tv;

    FD_ZERO(&rset);
    FD_ZERO(&wset);

    /* Listen for new incoming connections on main socket */
    if(server->socket >= 0)
        FD_SET(server->socket, &rset);

    foreach(connection, server->connections)
        if(connection->socket >= 0){
            if(connection->is_connected)
                FD_SET(connection->socket, &rset);
            if(connection->write_length || !connection->is_connected)
                FD_SET(connection->socket, &wset);
        } else if(connection->is_active && !connection->is_waiting_for_restart){
            /* Try to restart the active connection */
            server_add_timer(server, 1, 0, restart_connection_callback, connection);
            connection->is_waiting_for_restart = TRUE;
        }

    /* Listen for custom socket events if requested */
    if(server->custom_socket >= 0){
        if(server->custom_socket_read_hook)
            FD_SET(server->custom_socket, &rset);
        if(server->custom_socket_write_hook)
            FD_SET(server->custom_socket, &wset);
    }

    /* Timeout */
    tv.tv_sec = 0;
    tv.tv_usec = 1e3*wait;

    if(select(FD_SETSIZE, &rset, &wset, NULL, &tv) > 0){
        /* Check already connected clients */
        foreach(connection, server->connections){
            if(connection->socket >= 0 && FD_ISSET(connection->socket, &wset)){
                FD_CLR(connection->socket, &wset);

                if(!connection->is_connected){
                    /* Finalization of async connect() call */
                    int optval = -1;
                    socklen_t optval_len = sizeof(optval);

                    getsockopt(connection->socket, SOL_SOCKET, SO_ERROR, &optval, &optval_len);

                    if(!optval){
                        /* Successful connection */
                        SERVER_CALL_HOOK(server, connection_connected_hook, connection);

                        connection->is_connected = TRUE;
                    } else {
                        if(!connection->is_active)
                            dprintf("connect to %s:%d : %s\n",
                                    connection->host, connection->port, strerror(optval));
                        close(connection->socket);
                        connection->socket = -1;
                    }
                } else {
                    /* We actually may write to the socket */
                    server_connection_write(connection);
                }
            }

            if(connection->socket >= 0 && FD_ISSET(connection->socket, &rset)){
                /* Reading may destroy the connection, so let's keep previous one */
                connection_str *prev = connection->prev;

                FD_CLR(connection->socket, &rset);

                server_connection_read(server, connection);

                /* This is foreach() magic */
                connection = prev->next;
            }
        }

        /* Custom socket events? */
        if(server->custom_socket >= 0){
            if(FD_ISSET(server->custom_socket, &rset)){
                SERVER_CALL_HOOK(server, custom_socket_read_hook, server->custom_socket);
                FD_CLR(server->custom_socket, &rset);
            }
            if(FD_ISSET(server->custom_socket, &wset)){
                SERVER_CALL_HOOK(server, custom_socket_write_hook, server->custom_socket);
                FD_CLR(server->custom_socket, &wset);
            }
        }

        /* New incoming connection? */
        if(server->socket >= 0 && FD_ISSET(server->socket, &rset))
            server_accept(server);
    }

    /* Check expiration of timers */
    server_check_timers(server);
}

int server_has_connection(server_str *server, connection_str *connection)
{
    connection_str *conn = NULL;

    foreach(conn, server->connections)
        if(conn == connection)
            return TRUE;

    return FALSE;
}
