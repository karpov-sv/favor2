#ifndef DATAFILE_H
#define DATAFILE_H

#include "image.h"
#include "time_str.h"

typedef struct {
    char *filename;
    FILE *file;
    FILE *idx;

    long long int frame_first;
    long long int frame_last;
    time_str time_first;
    time_str time_last;

    int length;
    long long int file_size;
    struct frame_head *head;
} datafile_str;

datafile_str *datafile_create(char *);
void datafile_delete(datafile_str *);
/* Get N-th frame in datafile */
image_str *datafile_get_frame(datafile_str *, u_int64_t );
void datafile_add_frame(datafile_str *, image_str *);
void datafile_dump_info(datafile_str *);

#endif /* DATAFILE_H */
