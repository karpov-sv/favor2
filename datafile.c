#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "time_str.h"
#include "image.h"
#include "image_binary.h"

#include "datafile.h"

static char *get_index_name(char *name)
{
    char *result = NULL;

    if(name && strlen(name) > 4 && strstr(name, ".GRB2")){
        char *tmp;

        result = make_string("%s", name);
        tmp = strstr(result, ".GRB2");
        strcpy(tmp, ".IND2");
    } else if(name && strstr(name, ".IND2"))
        result = make_string("%s", name);

    return result;
}

/* FIXME: check the input filename */
datafile_str *datafile_create(char *filename)
{
    datafile_str *datafile = (datafile_str *)malloc(sizeof(datafile_str));
    char *index_name = get_index_name(filename);
    FILE *idx = fopen(index_name, "a+");
    int d;

    datafile->filename = make_string("%s", filename);
    datafile->file = NULL;
    datafile->idx = idx;
    datafile->file_size = get_file_size(filename);

    /* FIXME: any better way estimating the number of frames? */
    if(idx){
        long pos;
        fseek(idx, 0, SEEK_END);
        pos = ftell(idx);
        datafile->length = pos / sizeof(struct frame_head);
        datafile->head = (struct frame_head *)malloc(sizeof(struct frame_head)*datafile->length);
        rewind(idx);
    } else {
        datafile->length = 0;
        datafile->head = NULL;
    }

    if(datafile->length){
        for(d = 0; d < datafile->length; d++)
            fread(&datafile->head[d], 1, sizeof(struct frame_head), idx);

        datafile->frame_first = datafile->head[0].frame_number;
        datafile->frame_last  = datafile->head[datafile->length - 1].frame_number;
        datafile->time_first  = datafile->head[0].time;
        datafile->time_last   = datafile->head[datafile->length - 1].time;
    }

    dprintf("%d %Ld - %Ld %Ld\n", datafile->length, datafile->file_size,
            datafile->frame_first, datafile->frame_last);

    free(index_name);

    return datafile;
}

int datafile_guess_number(datafile_str *datafile, u_int64_t frame_number)
{
    int number = 0;

    while(datafile->head[number].frame_number < frame_number)
        number ++;

    if(datafile->head[number].frame_number == frame_number)
        return number;
    else
        return -1;
}

/* Get frame with given frame_number from datafile */
image_str *datafile_get_frame(datafile_str *datafile, u_int64_t frame_number)
{
    image_str *image = NULL;
    int number = datafile_guess_number(datafile, frame_number);

    if(number >= 0){
        u_int32_t pos = datafile->head[number].offset;
        u_int32_t  data_size = datafile->head[number].size - datafile->head[number].head_size;
        char *buffer = (char *)malloc(data_size);

        if(!datafile->file)
            datafile->file = fopen(datafile->filename, "r");

        if(datafile->file){
            fseek(datafile->file, pos + datafile->head[number].head_size /* sizeof(struct frame_head) */, SEEK_SET);
            fread(buffer, 1, data_size, datafile->file);

            image = image_create_from_data(&datafile->head[number], buffer);
        }

        free(buffer);
    }

    return image;
}

void datafile_add_frame(datafile_str *datafile, image_str *image)
{
    struct frame_head *head = NULL;
    int data_size = 0;
    char *data = NULL;

    if(!datafile->file)
        datafile->file = fopen(datafile->filename, "a+");

    fseek(datafile->file, 0, SEEK_END);

    datafile->head = realloc(datafile->head, sizeof(struct frame_head)*(datafile->length + 1));

    head = &datafile->head[datafile->length];

    data = create_data_from_image(image, head, &data_size);

    head->head_size = sizeof(struct frame_head);
    head->frame_number = image_keyword_get_int64(image, "FRAMENUMBER");
    head->offset = ftell(datafile->file);
    head->size = data_size + sizeof(struct frame_head);
    head->time = image->time;
    head->width = image->width;
    head->height = image->height;
    head->rel_win.x1 = 0;
    head->rel_win.y1 = 0;
    head->rel_win.x2 = image->width;
    head->rel_win.y2 = image->height;
    head->exposure = image_keyword_get_double(image, "EXPOSURE");
    head->amplification = image_keyword_get_int(image, "AMPLIFICATION");
    head->ra = image->coords.ra0;
    head->dec = image->coords.dec0;

    fwrite(head, sizeof(struct frame_head), 1, datafile->idx);
    fwrite(head, sizeof(struct frame_head), 1, datafile->file);
    fwrite(data, 1, data_size, datafile->file);

    fflush(datafile->idx);
    fflush(datafile->file);

    if(!datafile->length){
        datafile->frame_first = head->frame_number;
        datafile->time_first = head->time;
    }

    datafile->frame_last = head->frame_number;
    datafile->time_last = head->time;

    datafile->file_size = ftell(datafile->file);

    free(data);

    datafile->length ++;
}

void datafile_delete(datafile_str *datafile)
{
    if(!datafile)
        return;

    if(datafile->file)
        fclose(datafile->file);

    if(datafile->idx)
        fclose(datafile->idx);

    if(datafile->filename)
        free(datafile->filename);

    if(datafile->head)
        free(datafile->head);

    free(datafile);
}

void datafile_dump_info(datafile_str *datafile)
{
    char *time_first = time_str_get_time(datafile->time_first);
    char *time_last = time_str_get_time(datafile->time_last);

    printf("%s: %lld - %lld, %s - %s\n",
           datafile->filename,
           datafile->frame_first, datafile->frame_last,
           time_first, time_last);

    free(time_first);
    free(time_last);
}
