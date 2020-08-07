#include <unistd.h>
#include <math.h>
#include <string.h>

#include "utils.h"
#include "image.h"
#include "image_binary.h"

void VS2001_convert_10_to_16(const u_int8_t *buf1, u_int16_t *buf2, int pix)
{
    const u_int8_t *b1 = buf1;
    u_int16_t *b2 = buf2;
    int i;

    if(pix < 8)
        return;

    if((pix % 8) !=0 ){
        pix -= pix % 8;
    }

    for(i = 0; i < pix;){
        int j;
        u_int16_t bits;

        for(j = 0; j < 8; j++){
            b2[j] = b1[j] << 8;
        }

        b1 += 8;
        bits = *((u_int16_t *)(b1));

        b1 += 2;
        for(j = 0; j < 8; j++){
            b2[j] += (bits & 0x3) << 6;
            bits >>= 2;
        }

        b2 += 8;
        i += 8;
    }
}

void VS2001_convert_12_to_16(const u_int8_t *buf1, u_int16_t *buf2, int pix)
{
    const u_int8_t *b1 = buf1;
    u_int16_t *b2 = buf2;
    int i;

    if(pix < 4)
        return;

    if((pix % 4) != 0 ) {
        pix -= pix % 4;
    }

    for(i = 0; i < pix;) {
        int j;
        u_int16_t bits;

        for(j = 0; j < 4; j++){
            b2[j] = (u_int16_t)(b1[j]) << 4;
        }

        b1 += 4;

        bits = *((u_int16_t *)(b1));

        b1 += 2;

        for(j = 0; j < 4; j++){
            b2[j] += (bits & 0xF); /* was also  << 4; */
            bits >>= 4;
        }

        b2 += 4;
        i += 4;
    }
}

void CSDU_convert_12_to_16(const u_int8_t *buf1, u_int16_t *buf2, int pix)
{
    int i;
    int j = 0;

    if(pix < 4)
        return;

    if((pix % 4) != 0 ) {
        pix -= pix % 4;
    }

    for(i = 0; i < pix; i += 2) {
        buf2[i] = buf1[j++] << 4;
        buf2[i + 1] = buf1[j++] << 4;

        buf2[i] += buf1[j] & 0xF;
        buf2[i + 1] += buf1[j++] >> 4;
    }
}

image_str *image_create_from_data(struct frame_head *head, char *data)
{
    int width  = head->width;
    int height = head->height;
    image_str *image = image_create(width, height);

    image->time = head->time;

    image_keyword_add_double(image, "EXPOSURE", head->exposure, NULL);
    image_keyword_add_int64(image, "FRAMENUMBER", head->frame_number, NULL);
    image_keyword_add_int(image, "AMPLIFICATION", head->amplification, NULL);

    /* if(head->ra != 0 || head->dec != 0){ */
    /*     image_keyword_add_double(image, "RA0", head->ra, NULL); */
    /*     image_keyword_add_double(image, "DEC0", head->dec, NULL); */
    /* } */

    switch(head->frame_format){
    case FRAME_FORMAT_GENERAL_16:
        memcpy(image->data, data, 2*width*height);
        break;
    case FRAME_FORMAT_VS_10:
        VS2001_convert_10_to_16((u_int8_t *)data, image->data, image->width*image->height);
        break;
    case FRAME_FORMAT_VS_12:
        VS2001_convert_12_to_16((u_int8_t *)data, image->data, image->width*image->height);
        break;
    case FRAME_FORMAT_CSDU_12:
        CSDU_convert_12_to_16((u_int8_t *)data, image->data, image->width*image->height);
        break;
    default:
        dprintf("Unsupported frame format %d\n", head->frame_format);
        break;
    }

    return image;
}

void fill_frame_head(image_str *image, struct frame_head *head)
{
    strcpy(head->magic, "IMAGE HEADER DATAGRAM32");

    head->width = image->width;
    head->height = image->height;

    head->exposure = image_keyword_get_double(image, "EXPOSURE");
    head->frame_number = image_keyword_get_int64(image, "FRAMENUMBER");
    head->amplification = image_keyword_get_int(image, "AMPLIFICATION");
    head->ra = image_keyword_get_double(image, "RA0");
    head->dec = image_keyword_get_double(image, "DEC0");
    head->head_size = sizeof(struct frame_head) + 4;
    head->size = head->head_size + image->width*image->height*2;

    head->time = image->time;

    head->frame_format = FRAME_FORMAT_GENERAL_16;
}

char *create_data_from_image(image_str *image, struct frame_head *head, int *size_ptr)
{
    int data_size = image->width*image->height*2;
    char *data = malloc(data_size);

    memcpy(data, image->data, data_size);

    fill_frame_head(image, head);

    if(size_ptr)
        *size_ptr = data_size;

    return data;
}
