#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "utils.h"

#include "image.h"
#include "image_binary.h"
#include "simulator.h"
#include "server.h"
#include "command.h"

#include "ports.h"

int is_ds9 = FALSE;

typedef struct grabber_str {
    simulator_str *sim;

    char *udp_host;
    int udp_port;

    int is_sending;
    int is_quit;

    double fwhm;
    double amplify;

    double ra0;
    double dec0;
    double delta_ra;
    double delta_dec;

    double pixscale;
} grabber_str;

static void ds9_set_filename(char *filename)
{
    char *command = make_string("xpaset -p ds9 file fits %s", filename);

    system(command);

    free(command);
}

void send_udp_image(char *host, int port, image_str *image)
{
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    int length = 0;
    int pos = 0;
    struct frame_head head;
    char *data = create_data_from_image(image, &head, &length);
    struct sockaddr_in addr;
    struct hostent *hostinfo = gethostbyname(host);
    int optlen = sizeof(int);
    int sndbuf_size = 50000;
    int total = 0;

    /* Show image if requested */
    if(is_ds9){
        image_dump_to_fits(image, "/tmp/out_grabber.fits");
        ds9_set_filename("/tmp/out_grabber.fits");
    }

    /* Bind socket to a given port */
    addr.sin_family = PF_INET;
    addr.sin_port = htons(port);
    /* IP-address lookup */
    if(!hostinfo)
        hostinfo = gethostbyaddr(host, strlen(host), AF_INET);

    /* Lookup failure */
    if(!hostinfo)
        exit_with_error("hostname");

    addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

    if(sock < 0)
        exit_with_error("socket");

    /* Guess the maximal amount of data to be passed to sendto */
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (int *)&sndbuf_size, &optlen);    int sent0 = 0;
    /* dprintf("SNDBUF size is %d\n", sndbuf_size); */
    /* ...and don't let it be too large */
    sndbuf_size = MIN(sndbuf_size, 50000);

    /* if(setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, "lo", 3) < 0) */
    /*     exit_with_error("setsockopts2"); */

    if((sent0 = sendto(sock, &head, sizeof(head), 0, &addr, sizeof(addr))) <= 0)
        exit_with_error("sendto");

    total += sent0;

    while(pos < length){
        int sent = 0;

        if(pos + sndbuf_size <= length)
            sent = sendto(sock, data + pos, sndbuf_size, 0, &addr, sizeof(addr));
        else
            sent = sendto(sock, data + pos, length - pos, 0, &addr, sizeof(addr));

        if(sent < 0)
            exit_with_error("sendto");

        pos += sent;
        total += sent;

        usleep(1000);
    }

    free(data);

    close(sock);
}

void set_fwhm(grabber_str *grabber, double value)
{
    grabber->fwhm = value;
}

void set_amplify(grabber_str *grabber, double value)
{
    grabber->amplify = value;
    grabber->sim->amplify = value;
}

void update_simulator(grabber_str *grabber)
{
    double ra0 = grabber->ra0;
    double dec0 = grabber->dec0;

    coords_get_ra_dec_shifted(grabber->ra0, grabber->dec0, grabber->delta_ra, grabber->delta_dec, &ra0, &dec0);

    simulator_set_field(grabber->sim, ra0, dec0, grabber->pixscale);
    simulator_reset_events(grabber->sim);
}

void make_image(server_str *server, int type, void *data)
{
    grabber_str *grabber = (grabber_str *)data;
    image_str *image = NULL;

    if(grabber->fwhm != grabber->sim->fwhm){
        grabber->sim->fwhm = grabber->fwhm;
        simulator_update_stars(grabber->sim);
    }

    simulator_iterate(grabber->sim);

    image = simulator_get_image(grabber->sim);

    send_udp_image(grabber->udp_host, grabber->udp_port, image);

    /* dprintf("Image sent\n"); */

    image_delete(image);

    if(grabber->is_sending)
        server_add_timer(server, 0.1, 0, make_image, grabber);
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    grabber_str *grabber = (grabber_str *)data;
    command_str *command = command_parse(string);

    server_default_line_read_hook(server, connection, string, data);

    if(command_match(command, "exit") || command_match(command, "quit")){
        grabber->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "ping")){
        server_connection_message(connection, "ping_ok");
    } else if(command_match(command, "set_fwhm")){
        double value = 0;

        command_args(command,
                     "%lf", &value,
                     NULL);

        set_fwhm(grabber, value);

        server_connection_message(connection, "set_fwhm_ok");
    } else if(command_match(command, "set_amplify")){
        double value = 0;

        command_args(command,
                     "%lf", &value,
                     NULL);

        set_amplify(grabber, value);

        server_connection_message(connection, "set_amplify_ok");
    } else if(command_match(command, "set_pointing")){
        command_args(command,
                     "ra=%lf", &grabber->ra0,
                     "dec=%lf", &grabber->dec0,
                     "pixscale=%lf", &grabber->pixscale,
                     NULL);

        update_simulator(grabber);

        server_connection_message(connection, "set_pointing_done");
    } else if(command_match(command, "set_relative")){
        double delta_ra = 0;
        double delta_dec = 0;

        command_args(command,
                     "dra=%lf", &delta_ra,
                     "ddec=%lf", &delta_dec,
                     NULL);

        grabber->delta_ra = delta_ra;
        grabber->delta_dec = delta_dec;

        update_simulator(grabber);

        server_connection_message(connection, "set_relative_done");
    } else if(command_match(command, "get_fix")){
        server_connection_message(connection, "fix ra=%g dec=%g", grabber->ra0, grabber->dec0);
    } else if (!strcmp(string, "CONTROL NETSTART")){
        if(!grabber->is_sending)
            server_add_timer(server, 0.1, 0, make_image, grabber);
        grabber->is_sending = TRUE;
    } else if (!strcmp(string, "CONTROL NETSTOP")){
        grabber->is_sending = FALSE;
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

int main(int argc, char **argv)
{
    grabber_str *grabber = (grabber_str *)malloc(sizeof(grabber_str));
    server_str *server = server_create();
    char *udp_host = "localhost";
    int udp_port = PORT_GRABBER_UDP;
    int port = PORT_GRABBER_CONTROL;
    int width = 600;
    int height = 600;
    double ra0 = 56.75;
    double dec0 = 24.11;
    double pixscale = 25.9;
    int is_quiet = FALSE;

    parse_args(argc, argv,
               "udp_host=%s", &udp_host,
               "udp_port=%d", &udp_port,
               "port=%d", &port,
               "width=%d", &width,
               "height=%d", &height,
               "ra0=%lf", &ra0,
               "dec0=%lf", &dec0,
               "pixscale=%lf", &pixscale,
               "-ds9", &is_ds9,
               "-quiet", &is_quiet,
               NULL);

    SERVER_SET_HOOK(server, line_read_hook, process_command, grabber);

    server_listen(server, "localhost", port);

    grabber->sim = simulator_create(width, height);
    grabber->udp_host = udp_host;
    grabber->udp_port = udp_port;

    grabber->ra0 = ra0;
    grabber->dec0 = dec0;
    grabber->delta_ra = 0;
    grabber->delta_dec = 0;
    grabber->fwhm = grabber->sim->fwhm;
    grabber->amplify = 1;
    grabber->pixscale = pixscale;

    grabber->sim->readout = 100;

    update_simulator(grabber);

    if(is_quiet){
        dprintf("Quiet run, no transients will be generated\n");
        grabber->sim->satellite_probability = 0.0;
        grabber->sim->meteor_probability = 0.0;
        grabber->sim->flash_probability = 0.0;
    } else {
        grabber->sim->satellite_probability = 0.001;
        grabber->sim->meteor_probability = 0.001;
        grabber->sim->flash_probability = 0.001;
    }

    set_amplify(grabber, 0);

    grabber->is_sending = FALSE;
    grabber->is_quit = FALSE;

    while(!grabber->is_quit)
        server_cycle(server, 10);

    server_delete(server);
    simulator_delete(grabber->sim);

    return EXIT_SUCCESS;
}
