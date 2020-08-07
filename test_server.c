#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

#include "server.h"

void timer_callback(server_str *server, int type, void *data)
{
    dprintf("Timer %d expired\n", type);

    server_add_timer(server, 1, type + 1, timer_callback, NULL);
}

int main(int argc, char **argv)
{
    server_str *server = server_create();
    char *host = "localhost";
    int port = 5555;
    char *message = "test";
    int is_server = FALSE;

    parse_args(argc, argv,
               "-server", &is_server,
               "port=%d", &port,
               "%s", &message,
               NULL);
    
    if(is_server){
        server_listen(server, host, port);

        //server_add_timer(server, 2, 1, timer_callback, NULL);
        
        while(1){
            server_cycle(server, 100);

            //dprintf("%d timers active\n", list_length(&server->timers));
        }
    } else {
        connection_str *conn = server_add_connection(server, host, port);

        conn->is_active = TRUE;
        
        server_connection_message(conn, message);

        while(1){
            server_cycle(server, 100);
        }
    }
    
    server_delete(server);
    
    return EXIT_SUCCESS;
}
