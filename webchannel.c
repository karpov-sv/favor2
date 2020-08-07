#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "utils.h"

#include "image.h"
#include "server.h"
#include "ports.h"
#include "command.h"

typedef struct {
    server_str *server;
    connection_str *channel_connection;

    GHashTable *streams;
    GHashTable *singles;

    char *channel_string;

    double delay;

    int is_quit;
} webchannel_str;

#define TYPE_FRAME 100
#define TYPE_STATUS 101

static void webchannel_request_frame(server_str *server, int type, void *data)
{
    webchannel_str *web = (webchannel_str *)data;

    server_connection_message(web->channel_connection, "get_current_frame");
}

static void webchannel_request_status(server_str *server, int type, void *data)
{
    webchannel_str *web = (webchannel_str *)data;

    if(web->channel_connection && web->channel_connection->is_connected)
       server_connection_message(web->channel_connection, "get_channel_status");
    else {
        if(web->channel_string)
            free(web->channel_string);
        web->channel_string = make_string("channel disconnected");
    }

    server_add_timer(server, 0.5, TYPE_STATUS, webchannel_request_status, web);
}

static void binary_read_hook(server_str *server, connection_str *connection, char *buffer, int length, void *data)
{
    webchannel_str *web = (webchannel_str *)data;
    connection_str *ctmp = NULL;

    foreach(ctmp, server->connections){
        if(g_hash_table_lookup(web->streams, ctmp)){
            server_connection_message_nozero(ctmp, "--boundarydonotcross\n"
                                             "Content-Type: image/jpeg\n"
                                             "Content-Length: %d\n"
                                             "X-Timestamp: 0.000000\n\n", length);
            server_connection_write_block(ctmp, buffer, length);
            server_connection_message_nozero(ctmp, "\n");
        }

        if(g_hash_table_lookup(web->singles, ctmp)){
            server_connection_message_nozero(ctmp, "Content-Type: image/jpeg\n"
                                             "Content-Length: %d\n"
                                             "X-Timestamp: 0.000000\n\n", length);
            server_connection_write_block(ctmp, buffer, length);
            server_connection_message_nozero(ctmp, "\n");
            g_hash_table_remove(web->singles, ctmp);
        }
    }

    if(!server_number_of_timers_with_type(web->server, TYPE_FRAME) &&
       (g_hash_table_size(web->streams) || g_hash_table_size(web->singles)))
        server_add_timer(server, web->delay, TYPE_FRAME, webchannel_request_frame, web);
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    webchannel_str *web = (webchannel_str *)data;
    command_str *command = command_parse(string);

    /* if(!connection->is_binary) */
    /*     server_default_line_read_hook(server, connection, string, data); */

    if(command_match(command, "current_frame")){
        int length = 0;

        command_args(command, "length=%d", &length, NULL);

        server_connection_wait_for_binary(connection, length);
    } else if(command_match(command, "channel_status")){
        if(web->channel_string)
            free(web->channel_string);
        web->channel_string = make_string("%s", string);
    } else if(command_match(command, "GET")){
        char *url = NULL;
        char *proto = NULL;

        command_args(command, "url=%s", &url, "proto=%s", &proto, NULL);

        dprintf("GET %s\n", url);
        if(!strncmp(url, "/stream", 7)){
            server_connection_message_nozero(connection,
                                             "HTTP/1.0 200 OK\n"
                                             "Connection: close\n"
                                             "Server: FAVOR2-Channel\n"
                                             "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\n"
                                             "Pragma: no-cache\n"
                                             "Expires: Mon, 3 Jan 2000 12:34:56 GMT\n"
                                             "Content-Type: multipart/x-mixed-replace;boundary=boundarydonotcross\n\n");

            g_hash_table_insert(web->streams, connection, connection);
            if(!server_number_of_timers_with_type(web->server, TYPE_FRAME))
                webchannel_request_frame(server, 0, web);
        } else if(!strncmp(url, "/current", 7)){
            server_connection_message_nozero(connection,
                                             "HTTP/1.0 200 OK\n"
                                             "Connection: close\n"
                                             "Server: FAVOR2-Channel\n"
                                             "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\n"
                                             "Pragma: no-cache\n"
                                             "Expires: Mon, 3 Jan 2000 12:34:56 GMT\n");

            g_hash_table_insert(web->singles, connection, connection);
            if(!server_number_of_timers_with_type(web->server, TYPE_FRAME))
                webchannel_request_frame(server, 0, web);
        } else if(!strncmp(url, "/status", 7)){
            server_connection_message_nozero(connection,
                                             "HTTP/1.0 200 OK\n"
                                             "Connection: close\n"
                                             "Server: FAVOR2-Channel\n"
                                             "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\n"
                                             "Pragma: no-cache\n"
                                             "Expires: Mon, 3 Jan 2000 12:34:56 GMT\n"
                                             "Refresh: 20\n"
                                             "Content-Type: text/html\n\n"
                                             "<html><head><title='Channel'></head><body>"
                                             /* "<h1>Channel</h1>" */
                                             "<p align=center>Timestamp: %s"
                                             "<p align=center>%s"
                                             "<p align=center><img src='current.jpg' align=center>"
                                             "<p align=center><a href='stream.mjpeg'>Live stream</a>"
                                             "</body></html>", timestamp(), web->channel_string);
            connection->is_finished = TRUE;
        } else
            connection->is_finished = TRUE;
    }

    command_delete(command);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    webchannel_str *web = (webchannel_str *)data;

    server_default_connection_connected_hook(server, connection, data);

    /* Restart the timer if it is lost due to reconnection to the channel */
    if(connection == web->channel_connection && !server_number_of_timers_with_type(web->server, TYPE_FRAME) &&
       (g_hash_table_size(web->streams) || g_hash_table_size(web->singles)))
        server_add_timer(server, web->delay, TYPE_FRAME, webchannel_request_frame, web);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    webchannel_str *web = (webchannel_str *)data;

    if(g_hash_table_lookup(web->streams, connection))
        g_hash_table_remove(web->streams, connection);
    if(g_hash_table_lookup(web->singles, connection))
        g_hash_table_remove(web->singles, connection);
}

int main(int argc, char **argv)
{
    webchannel_str *web = (webchannel_str *)malloc(sizeof(webchannel_str));
    char *channel_host = "localhost";
    int channel_port = PORT_CHANNEL_CONTROL;
    int port = PORT_WEBCHANNEL;

    web->delay = 1;

    parse_args(argc, argv,
               "port=%d", &port,
               "channel_host=%s", &channel_host,
               "channel_port=%d", &channel_port,
               "delay=%lf", &web->delay,
               NULL);

    web->server = server_create();

    server_listen(web->server, "localhost", port);

    SERVER_SET_HOOK(web->server, line_read_hook, process_command, web);
    SERVER_SET_HOOK(web->server, connection_connected_hook, connection_connected, web);
    SERVER_SET_HOOK(web->server, connection_disconnected_hook, connection_disconnected, web);

    web->channel_connection = server_add_connection(web->server, channel_host, channel_port);
    web->channel_connection->is_active = TRUE;
    web->channel_connection->binary_read_hook = binary_read_hook;
    web->channel_connection->binary_read_hook_data = web;

    web->streams = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    web->singles = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    web->channel_string = NULL;

    webchannel_request_status(web->server, 0, web);

    web->is_quit = FALSE;

    while(!web->is_quit)
        server_cycle(web->server, 1);

    g_hash_table_destroy(web->streams);
    g_hash_table_destroy(web->singles);

    server_delete(web->server);
    free(web);

    return EXIT_SUCCESS;
}
