#ifndef BEHOLDER_H
#define BEHOLDER_H

#include "server.h"
#include "command_queue.h"
#include "script.h"
#include "db.h"

struct beholder_str;

typedef struct {
    connection_str *connection;

    int id;

    double ra;
    double dec;

    int state;
    char *status;
} mount_str;

typedef struct {
    connection_str *connection;

    int id;

    /* The mount on which channel is mounted */
    mount_str *mount;

    /* FOV center as reported by channel */
    double ra;
    double dec;

    int state; /* State as reported by channel */
    char *status; /* All the variables reported by channel as single string, prefixed with channel<id>_ */
    int is_requesting_frame;

    /* Current frame as a JPEG byte array */
    char *frame;
    int frame_length;
    time_str frame_time;
} channel_str;

typedef struct {
    /* Main network connector */
    server_str *server;

    /* Uplink connections to various subsystems */
    connection_str *weather_connection;
    connection_str *dome_connection;
    connection_str *scheduler_connection;
    connection_str *can_connection;
    connection_str *logger_connection;

    /* Statuses */
    char *weather_status;
    char *dome_status;
    char *scheduler_status;
    char *scheduler_suggestion;
    char *can_status;

    int is_night;
    int is_weather_good;
    int is_dome_open;

    int force_night;

    int nmounts;
    int nchannels;

    mount_str **mount;
    channel_str **channel;

    script_str *script;

    command_queue_str *commands;

    db_str *db;

    char *state;

    int is_quit;
} beholder_str;

beholder_str *beholder_create();
void beholder_delete();

channel_str *channel_create(beholder_str *, int );
void channel_delete(channel_str *);

mount_str *mount_create(int );
void mount_delete(mount_str *);

#endif /* BEHOLDER_H */
