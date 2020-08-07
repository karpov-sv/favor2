#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "utils.h"

#include "ports.h"
#include "server.h"
#include "command.h"
#include "coords.h"

typedef struct {
    server_str *server;

    connection_str *remos_connection;

    double target_ra;
    double target_dec;
    int state;
    int dome;

    int is_quit;
} rem_str;

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    rem_str *rem = (rem_str *)data;
    command_str *command = command_parse(string);

    if(connection)
        server_default_line_read_hook(server, connection, string, data);
    else
        dprintf("Internal message: %s\n", string);

    if(command_match(command, "exit") || command_match(command, "quit")){
        rem->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "REM_NIGHT_BEGIN")){
        server_broadcast_message(server, "night_begin");
    } else if(command_match(command, "REM_NIGHT_END")){
        server_broadcast_message(server, "night_end");
    } else if(command_match(command, "REM_DOME_OPENED")){
        /* server_broadcast_message(server, "dome_open"); */
    } else if(command_match(command, "REM_DOME_CLOSED")){
        /* server_broadcast_message(server, "dome_close"); */
    } else if(command_match(command, "REM_LIGHT_ON")){
        server_broadcast_message(server, "indome_light_on");
    } else if(command_match(command, "REM_LIGHT_OFF")){
        server_broadcast_message(server, "indome_light_off");
    } else if(command_match(command, "REM_TRACKING_STARTED")){
        /* server_broadcast_message(server, "tracking_start"); */
    } else if(command_match(command, "REM_TRACKING_STOPPED")){
        /* server_broadcast_message(server, "tracking_stop"); */
    } else if(command_match(command, "REM_WILL_REPOINT_TO")){
        double ra = 0;
        double dec = 0;
        double moon = 0;
        char *name = NULL;

        command_args(command,
                     "ra=%lf", &ra,
                     "dec=$lf", &dec,
                     "moon=%lf", &moon,
                     "name=%s", &name,
                     NULL);

        /* server_broadcast_message(server, "next_target ra=%g dec=%g moon=%g name=%s", ra, dec, moon, name); */

        if(name)
            free(name);
    } else if(command_match(command, "telescope_state")){
        double ra = -1;
        double dec = -1;
        double target_ra = rem->target_ra;
        double target_dec = rem->target_dec;
        char *name = NULL;
        int state = rem->state;
        int below = 0;
        int dome = rem->dome;

        command_args(command,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "target_ra=%lf", &target_ra,
                     "target_dec=%lf", &target_dec,
                     "target_name=%s", &name,
                     "state=%d", &state,
                     "below=%d", &below,
                     "dome=%d", &dome,
                     NULL);

        server_broadcast_message(server, "current_pointing ra=%g dec=%g target_ra=%g target_dec=%g target_name=%s tracking=%d below=%d open=%d",
                                 ra, dec, target_ra, target_dec, name ? name : "unknown", state == 3 ? 1 : 0, below > 0 ? 1 : 0, dome == 48 ? 1 : 0);

        if(rem->state != state){
            if(state == 3)
                server_broadcast_message(server, "tracking_start");
            else if(rem->state == 3)
                server_broadcast_message(server, "tracking_stop");
        }

        if(rem->dome != dome){
            if(dome == 48)
                server_broadcast_message(server, "dome_open");
            else if(rem->dome == 48)
                server_broadcast_message(server, "dome_close");
        }

        if(coords_sky_distance(rem->target_ra, rem->target_dec, target_ra, target_dec) > 0.01)
            server_broadcast_message(server, "next_target ra=%g dec=%g", target_ra, target_dec);

        rem->state = state;
        rem->dome = dome;
        rem->target_ra = target_ra;
        rem->target_dec = target_dec;
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

/* REMOS message fields */
#define REMOS_FIELD_TYPE            0
#define REMOS_FIELD_LOGICAL         1
#define REMOS_FIELD_RA              2
#define REMOS_FIELD_DEC             3
#define REMOS_FIELD_MOON_DISTANCE   4
#define REMOS_FIELD_DOME_CONNECTION 10
#define REMOS_FIELD_DOME_STATUS     11
#define REMOS_FIELD_TARGET_NAME     25

/* REMOS message types */
#define REMOS_TRACKING    501
#define REMOS_NIGHT_BEGIN 502
#define REMOS_NIGHT_END   503
#define REMOS_DOME        504
#define REMOS_LIGHT       505
#define REMOS_TARGET      506
#define REMOS_IMALIVE     507
/* Int - double conversion factor */
#define REMOS_RADECFACT   100000

static void process_rem_data(server_str *server, int sock, void *data)
{
    rem_str *rem = (rem_str *)data;
    int packet_size = 40;
    int buffer[packet_size];
    struct sockaddr_in address;
    int address_length = sizeof(address);
    int length = 0;
    int d;

    length = recvfrom(server->custom_socket, buffer, packet_size*sizeof(int), 0, (struct sockaddr *)&address, (socklen_t *)&address_length);

    if(length == packet_size*sizeof(int)){
        int logical = 0;
        double ra = 0;
        double dec = 0;
        double moon = 0;

        /* Return the packet back - I'm Alive! */
        {
            int tmp = buffer[REMOS_FIELD_TYPE];

            buffer[REMOS_FIELD_TYPE] = htonl(REMOS_IMALIVE);

            sendto(server->custom_socket, buffer, packet_size*sizeof(int), 0, (struct sockaddr *)&address, address_length);

            buffer[REMOS_FIELD_TYPE] = tmp;
        }

        /* Convert the packet data from network to host byte order */
        for(d = 0; d < packet_size; d++)
            buffer[d] = ntohl(buffer[d]);

        /* Parse message */
        logical = buffer[REMOS_FIELD_LOGICAL];
        ra = buffer[REMOS_FIELD_RA]*1./REMOS_RADECFACT/15;
        dec = buffer[REMOS_FIELD_DEC]*1./REMOS_RADECFACT;
        moon = buffer[REMOS_FIELD_MOON_DISTANCE]*1./REMOS_RADECFACT;

        if(buffer[REMOS_FIELD_TYPE] != REMOS_IMALIVE)
            dprintf("rem_server: UDP message from %s, type=%d at %s\n",
                    inet_ntoa(address.sin_addr), buffer[REMOS_FIELD_TYPE], timestamp(time_current()));

        switch(buffer[REMOS_FIELD_TYPE]){
        case REMOS_TRACKING:
            /* logical meanings: 1 - idle, 2 - slewing, 3 - tracking */
            if(logical == 3)
                process_command(server, NULL, "REM_TRACKING_STARTED", rem);
            else
                process_command(server, NULL, "REM_TRACKING_STOPPED", rem);
            break;
        case REMOS_LIGHT:
            if(logical == TRUE)
                process_command(server, NULL, "REM_LIGHT_ON", rem);
            else
                process_command(server, NULL, "REM_LIGHT_OFF", rem);
            break;
        case REMOS_NIGHT_BEGIN:
            process_command(server, NULL, "REM_NIGHT_BEGIN", rem);
            break;
        case REMOS_NIGHT_END:
            process_command(server, NULL, "REM_NIGHT_END", rem);
            break;
        case REMOS_DOME:
            /* Dome connection status is in REMOS_FIELD_DOME_CONNECTION, 0 - disconnected, 1 - connected
             * Dome status is in REMOS_FIELD_DOME_STATUS
             * 0 - closed, 5 - opening, 10- closing, 48 - dome open
             * 18, 20, 33, 40 - dome failure */
            if(buffer[REMOS_FIELD_DOME_STATUS] == 48)
                process_command(server, NULL, "REM_DOME_OPENED", rem);
            else
                process_command(server, NULL, "REM_DOME_CLOSED", rem);
            break;
        case REMOS_TARGET:
            {
                char *name = NULL;
                char *string = NULL;

                /* FIXME: check sanity of the string */
                if(buffer[REMOS_FIELD_TARGET_NAME]){
                    unsigned char *pos = (unsigned char *)(buffer + REMOS_FIELD_TARGET_NAME);

                    name = NULL;

                    /* Object name is given to us as an array of INTs, not CHARs */
                    while(*pos){
                        add_to_string(&name, "%c", *pos);
                        pos += sizeof(int);
                    }
                } else
                    name = make_string("UNKNOWN");

                string = make_string("REM_WILL_REPOINT_TO ra=%g dec=%g moon=%g name=%s",
                                     ra, dec, moon, name);

                process_command(server, NULL, string, rem);

                free(string);
                free(name);
            }

            break;
        }
    }
}

void attach_rem_socket(rem_str *rem, int udp_port)
{
    struct sockaddr_in addr;
    int sock = socket(PF_INET, SOCK_DGRAM, 0);

    /* Bind socket to a given device */
    addr.sin_family = PF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(udp_port);

    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        exit_with_error("bind");

    rem->server->custom_socket = sock;
    SERVER_SET_HOOK(rem->server, custom_socket_read_hook, process_rem_data, rem);

    dprintf("Listening for UDP packets on port %d\n", udp_port);
}

int main(int argc, char **argv)
{
    rem_str *rem = (rem_str *)malloc(sizeof(rem_str));
    char *remos_host = "10.56.2.4";
    int remos_port = 12345;
    int udp_port = PORT_REM_UDP;
    int port = PORT_REM;

    parse_args(argc, argv,
               "remos_host=%s", &remos_host,
               "remos_port=%d", &remos_port,
               "udp_port=%d", &udp_port,
               NULL);

    rem->server = server_create();

    rem->target_ra = -1;
    rem->target_dec = -1;
    rem->state = 0;
    rem->dome = -1;

    rem->is_quit = FALSE;

    SERVER_SET_HOOK(rem->server, line_read_hook, process_command, rem);

    server_listen(rem->server, "localhost", port);

    rem->remos_connection = server_add_connection(rem->server, remos_host, remos_port);
    rem->remos_connection->is_active = TRUE;

    attach_rem_socket(rem, udp_port);

    while(!rem->is_quit)
        server_cycle(rem->server, 10);

    server_delete(rem->server);

    return EXIT_SUCCESS;
}
