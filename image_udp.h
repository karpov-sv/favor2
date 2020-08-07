#ifndef IMAGE_UDP_H
#define IMAGE_UDP_H

#include "image.h"
#include "image_binary.h"
#include "server.h"

typedef struct {
    int active;        /* FALSE - inactive, TRUE - collecting data */
    u_int64_t series;  /* Current series */
    int packets;       /* Total number of packets */
    int packet;        /* Current packet we waiting for */
    int data_size;
    int data_pos;      /* Current position inside data */
    u_int8_t *data;    /* packet data */
} image_udp_packet_str;

typedef struct {
    /* UDP packet max length */
    int udp_length;

    /* Current packet */
    image_udp_packet_str packet;

    /* Callbacks to get the raw data and/or processed image */
    void (*callback_raw)(struct frame_head *, void *);
    void *callback_raw_data;
    void (*callback_image)(image_str *, void *);
    void *callback_image_data;
} image_udp_str;

image_udp_str *image_udp_attach(server_str *, int );
void image_udp_detach(server_str *, image_udp_str *);

#endif /* IMAGE_UDP_H */
