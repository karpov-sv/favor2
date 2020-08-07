#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "utils.h"

#include "image_udp.h"

static void udp_read_hook(server_str *server, int sock, void *data)
{
    image_udp_str *udp = (image_udp_str *)data;
    u_int8_t buffer[udp->udp_length];
    struct frame_head *head = (struct frame_head *) &buffer;
    image_udp_packet_str *packet = &udp->packet;
    int length = recvfrom(sock, buffer, udp->udp_length, 0, 0, 0);

    /* TODO: there are a lot to optimize here memory-wise */

    /* Sanity check */
    if(length >= 26){
        if(!strncmp((char *)&head->magic, "IMAGE HEADER DATAGRAM32", 24)){
            /* First packet in a series */
            packet->active = TRUE;
            packet->series = head->frame_number;
            packet->data_size = head->size;
            packet->data_pos = head->head_size; //sizeof(struct frame_head);
            /* Create header as create_image_from_data wants it */
            packet->data = realloc(packet->data, packet->data_size);
            memcpy(packet->data, head, head->head_size/* sizeof(struct frame_head) */);
        } else if(!strncmp((char *)&head->magic, "IMAGE HEADER DATAGRAM", 22)){
            /* Legacy TORTORA-aged format - we will try to convert it to new one */
            struct tortora_frame_head *thead = (struct tortora_frame_head *)head;

            /* First packet in a series */
            packet->active = TRUE;
            packet->series = thead->frame_number;
            packet->data_size = thead->data_size + sizeof(struct frame_head) - length;
            packet->data_pos = sizeof(struct frame_head);
            /* Create header as create_image_from_data wants it */
            packet->data = realloc(packet->data, packet->data_size);
            /* ..and fill it from old-format TORTORA one */
            head = (struct frame_head *)packet->data;
            strcpy(head->magic, "IMAGE HEADER DATAGRAM32");
            head->head_size = sizeof(struct frame_head);
            head->size = head->head_size + thead->data_size;

            head->frame_number = thead->frame_number;
            head->width  = thead->rel_win.x2 - thead->rel_win.x1;
            head->height = thead->rel_win.y2 - thead->rel_win.y1;
            head->frame_format = FRAME_FORMAT_VS_12;
            head->vs_number = thead->vs_number;
            head->exposure = thead->exposure;
            head->amplification = thead->amplification;

            head->time.year = thead->time.year;
            head->time.month = thead->time.month;
            head->time.day = thead->time.day;
            head->time.hour = thead->time.hour;
            head->time.minute = thead->time.minute;
            head->time.second = thead->time.second;
            head->time.microsecond = 1e3*thead->time.millisecond;
        } else if(packet->active){
            if(packet->data_pos + length <= packet->data_size){
                /* Following packets of this series */
                /* int length = (packet->data_pos + udp->udp_length < packet->data_size) */
                /*     ? udp->udp_length */
                /*     : packet->data_size - packet->data_pos; */

                memcpy(packet->data + packet->data_pos, buffer, length);
                packet->data_pos += length;
            } else {
                /* Packets flow is broken, reset */
                packet->active = FALSE;
            }
        }

        /* Got last packet of a series? Send it to processor. It will free the memory itself. */
        if(packet->active && packet->data && packet->data_pos == packet->data_size){
            if(udp->callback_image){
                image_str *image = image_create_from_data((struct frame_head *)packet->data,
                                                          (char *)packet->data + ((struct frame_head *)packet->data)->head_size/* sizeof(struct frame_head) */);

                /* Image will be deleted by the client, if necessary */
                udp->callback_image(image, udp->callback_image_data);
            }

            if(udp->callback_raw)
                /* Data will be deleted by the client */
                udp->callback_raw((struct frame_head *)packet->data, udp->callback_raw_data);
            else
                free(packet->data);

            packet->data = NULL;
            packet->active = FALSE;
        }
    }
}

image_udp_str *image_udp_attach(server_str *server, int udp_port)
{
    image_udp_str *udp = (image_udp_str *)malloc(sizeof(image_udp_str));
    int sock = socket(PF_INET, SOCK_DGRAM, 0);

    udp->packet.active = FALSE;
    udp->packet.data = NULL;

    udp->callback_raw = NULL;
    udp->callback_raw_data = NULL;

    udp->callback_image = NULL;
    udp->callback_image_data = NULL;

    if(sock >= 0){
        struct sockaddr_in addr;
        int size = 1310710;

        /* Bind socket to a given port */
        addr.sin_family = PF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(udp_port);

        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(int));

        if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
            return_with_error("bind");

        udp->udp_length = 65507; /* FIXME: must be got at runtime */

        server->custom_socket = sock;
        SERVER_SET_HOOK(server, custom_socket_read_hook, udp_read_hook, udp);
    } else
        exit_with_error("udp_socket");

    return udp;
}

void image_udp_detach(server_str *server, image_udp_str *udp)
{
    close(server->custom_socket);

    SERVER_SET_HOOK(server, custom_socket_read_hook, NULL, NULL);

    if(udp->packet.data)
        free(udp->packet.data);

    free(udp);
}
