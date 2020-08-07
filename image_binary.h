#ifndef IMAGE_BINARY_H
#define IMAGE_BINARY_H

#include "time_str.h"
#include "image.h"

/* FIXME: isolate all internal structs and functions */

enum frame_format {
    FRAME_FORMAT_0,                 /* Unknown format */
    FRAME_FORMAT_GENERAL_8,         /* Unpacked 8bpp */
    FRAME_FORMAT_GENERAL_16,        /* Unpacked 16bpp */
    FRAME_FORMAT_VS_10,             /* VS2001 10bpp */
    FRAME_FORMAT_VS_12,             /* VS2001 12bpp */
    FRAME_FORMAT_CSDU_12            /* CSDU 12bpp */
};

typedef struct rect_str {
    u_int32_t x1 __attribute__ ((packed));
    u_int32_t y1 __attribute__ ((packed));
    u_int32_t x2 __attribute__ ((packed));
    u_int32_t y2 __attribute__ ((packed));
} rect_str;

struct frame_head {
    char magic[40]; /* Must be "IMAGE HEADER DATAGRAM32" */
    u_int32_t head_size __attribute__ ((packed));
    u_int32_t size __attribute__ ((packed)); /* head_size + data_size */
    u_int32_t offset __attribute__ ((packed));

    char camera_name[100];
    enum frame_format frame_format __attribute__ ((packed));
    u_int64_t frame_number __attribute__ ((packed));
    u_int32_t width __attribute__ ((packed));
    u_int32_t height __attribute__ ((packed));
    u_int32_t binning __attribute__ ((packed));
    rect_str rel_win;
    time_str time;
    u_int32_t vs_number __attribute__ ((packed));
    double exposure __attribute__ ((packed));
    int32_t amplification __attribute__ ((packed));
    int32_t contrast __attribute__ ((packed));
    int32_t brightness __attribute__ ((packed));
    double ra __attribute__ ((packed));
    double dec __attribute__ ((packed));
    int32_t moving __attribute__ ((packed));
    double temperature __attribute__ ((packed));
    int32_t filter __attribute__ ((packed));
};

struct tortora_time_str {
    u_int16_t year __attribute__ ((packed));
    u_int16_t month __attribute__ ((packed));
    u_int16_t wday __attribute__ ((packed));
    u_int16_t day __attribute__ ((packed));
    u_int16_t hour __attribute__ ((packed));
    u_int16_t minute __attribute__ ((packed));
    u_int16_t second __attribute__ ((packed));
    u_int16_t millisecond __attribute__ ((packed));
};

struct tortora_frame_head {
    char      magic[22]; /* "IMAGE HEADER DATAGRAM" with \0, 22 bytes */
    u_int32_t udp_head_size __attribute__ ((packed));
    u_int64_t frame_number __attribute__ ((packed));
    u_int32_t data_size __attribute__ ((packed));
    u_int32_t unused __attribute__ ((packed));
    struct tortora_time_str time;
    u_int32_t vs_number __attribute__ ((packed));
    rect_str  rel_win;
    rect_str  win;
    double    exposure __attribute__ ((packed));
    u_int32_t amplification __attribute__ ((packed));
};

/* Functions to decode packed data */
void VS2001_convert_10_to_16(const u_int8_t *, u_int16_t *, int );
void VS2001_convert_12_to_16(const u_int8_t *, u_int16_t *, int );
void CSDU_convert_12_to_16(const u_int8_t *, u_int16_t *, int );

void fill_frame_head(image_str *, struct frame_head *);
image_str *image_create_from_data(struct frame_head *, char *);
char *create_data_from_image(image_str *, struct frame_head *, int *);

#endif /* IMAGE_BINARY_H */
