#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"

#include "server.h"
#include "image_udp.h"

#include "ports.h"

char* image_dir = NULL;
int frame_index = 0;
int is_ds9 = FALSE;
int is_jpeg = FALSE;

static void ds9_set_filename(char *filename)
{
    char *command = make_string("xpaset -p ds9 file fits %s", filename);

    system(command);

    free(command);
}

void process_image(image_str *image, void *data)
{
    if (image_dir && strlen(image_dir)) {
        char *filename = make_string("%s/f%06d.%s", image_dir, frame_index, is_jpeg ? "jpg" : "fits");

        dprintf("Image #%d acquired at %s", frame_index, timestamp());
        if(is_jpeg)
            image_dump_to_jpeg(image, filename);
        else
            image_dump_to_fits(image, filename);
        dprintf(" and written at %s to %s\n", timestamp(), filename);

        frame_index++;

        free(filename);
    } else {
        dprintf("Image acquired at %s\n", timestamp());
    }

    if(is_ds9){
        image_dump_to_fits(image, "/tmp/test_udp_ds9_image.fits");
        ds9_set_filename("/tmp/test_udp_ds9_image.fits");
    }

    image_delete(image);
}

int main(int argc, char **argv)
{
    server_str *server = server_create();
    image_udp_str *udp = NULL;
    int udp_port = PORT_GRABBER_UDP;

    frame_index = 0;
    image_dir = NULL;

    parse_args(argc, argv,
               "udp_port=%d", &udp_port,
               "imgdir=%s", &image_dir,
               "-ds9", &is_ds9,
               "-jpeg", &is_jpeg,
               NULL);
    if (image_dir) {
        dprintf("Will write images to %s/\n", image_dir);
    }

    /* server_listen(server, host, port); */

    udp = image_udp_attach(server, udp_port);

    udp->callback_image = process_image;

    while(TRUE){
        server_cycle(server, 100);
    }

    image_udp_detach(server, udp);

    server_delete(server);

    return EXIT_SUCCESS;
}
