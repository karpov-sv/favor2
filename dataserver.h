#ifndef DATASERVER_H
#define DATASERVER_H

typedef struct {
    char *path;

    char **filenames;
    u_int64_t *uuids;
    time_str *times;

    int length;
} dataserver_str;

dataserver_str *dataserver_create(char *);
void dataserver_delete(dataserver_str *);
int dataserver_frame_first(dataserver_str *);
int dataserver_frame_last(dataserver_str *);
void dataserver_dump_info(dataserver_str *);
image_str *dataserver_get_frame(dataserver_str *, int );
image_str *dataserver_get_frame_cut(dataserver_str *, int , int , int , int , int );

int dataserver_find_uuid(dataserver_str *, u_int64_t );
int dataserver_find_time(dataserver_str *, time_str );

#endif /* DATASERVER_H */
