#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>

#include "utils.h"

#include "scheduler.h"
#include "server.h"
#include "ports.h"
#include "time_str.h"
#include "command.h"
#include "license.h"

#include "cJSON.h"

typedef struct scheduler_server_str {
    server_str *server;

    scheduler_str *scheduler;

    char *gloria_host;
    int gloria_port;
    GHashTable *gloria_connections;

    connection_str *logger_connection;
    connection_str *beholder_connection;

    int is_quit;
} scheduler_server_str;

static void get_scheduler_status(scheduler_server_str *ss, connection_str *conn)
{
    double zsun = scheduler_sun_zenith_angle(ss->scheduler, time_current());
    double zmoon = scheduler_moon_zenith_angle(ss->scheduler, time_current());
    char *status = make_string("scheduler_status zsun=%g zmoon=%g latitude=%lf longitude=%lf",
                               zsun, zmoon, ss->scheduler->latitude, ss->scheduler->longitude);
    scheduler_interest_str *interest = NULL;
    int ninterests = 0;

    foreach(interest, ss->scheduler->interests)
        if(interest->name && interest->radius){
            add_to_string(&status, " interest%d_name=%s interest%d_ra=%g interest%d_dec=%g interest%d_radius=%g",
                          ninterests, interest->name, ninterests, interest->ra,
                          ninterests, interest->dec, ninterests, interest->radius);

            ninterests ++;
        }

    add_to_string(&status, " ninterests=%d", ninterests);

    server_connection_message(conn, status);

    free(status);
}

void scheduler_log(scheduler_server_str *ss, const char *template, ...)
{
    va_list ap;
    char *buffer;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    dprintf("Log: %s\n", buffer);

    if(ss->logger_connection)
        server_connection_message(ss->logger_connection, buffer);

    free(buffer);
}

static void send_gloria_request(scheduler_server_str *ss, const char *template, ...)
{
    va_list ap;
    char *buffer = NULL;
    char *string = NULL;
    connection_str *conn = server_add_connection(ss->server, ss->gloria_host, ss->gloria_port);

    g_hash_table_insert(ss->gloria_connections, conn, conn);

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    string = make_string("GET %s HTTP/1.1\r\n\r\n", buffer);

    server_connection_message(conn, string);

    free(string);
    free(buffer);
}

static void process_gloria_JSON_plans(scheduler_server_str *ss, cJSON *plans)
{
    int nplans = cJSON_GetArraySize(plans);
    int d;

    for(d = 0; d < nplans; d++){
        cJSON *plan = cJSON_GetArrayItem(plans, d);
        char *status = cJSON_GetObjectItem(plan, "status")->valuestring;
        /* int target_id = cJSON_GetObjectItem(plan, "target_id")->valueint; */
        int id = cJSON_GetObjectItem(plan, "id")->valueint;
        double exposure = cJSON_GetObjectItem(plan, "exposure")->valuedouble;
        double min_altitude = cJSON_GetObjectItem(plan, "min_altitude")->valuedouble;
        double max_moon_altitude = cJSON_GetObjectItem(plan, "max_moon_altitude")->valuedouble;
        double min_moon_distance = cJSON_GetObjectItem(plan, "min_moon_distance")->valuedouble;
        char *filter = cJSON_GetObjectItem(plan, "filter")->valuestring;
        double ra = cJSON_GetObjectItem(plan, "ra")->valuedouble;
        double dec = cJSON_GetObjectItem(plan, "dec")->valuedouble;
        char *name = cJSON_GetObjectItem(plan, "name")->valuestring;
        char *uuid = cJSON_GetObjectItem(plan, "uuid")->valuestring;

        /* Process plans */
        if(!strcmp(status, "advertised")){
            if(scheduler_target_is_observable(ss->scheduler, time_current(), ra, dec, min_altitude, max_moon_altitude, min_moon_distance)){
                /* Approve the plan */
                scheduler_log(ss, "Plan %d approved: ra=%g dec=%g exposure=%g\n", id, ra, dec, exposure);
                send_gloria_request(ss, "/api/approve?id=%d", id);
            } else {
                /* Reject the plan */
                scheduler_log(ss, "Plan %d rejected: ra=%g dec=%g\n", id, ra, dec);
                send_gloria_request(ss, "/api/reject?id=%d", id);
            }

        } if(!strcmp(status, "confirmed")){
            /* We need to check the plan and accept or reject it */
            char *name_fixed = make_string("%s", name);
            char *pos = name_fixed;

            while(pos && *pos){
                if(!isalnum(*pos) && *pos != '_' && *pos != '-' && *pos !='+')
                    *pos = '_';

                pos ++;
            }

            /* Sanitize the exposure */
            if(exposure < 1.0)
                exposure = 1.0;

            if(exposure > 100.0)
                exposure = 100.0;

            /* We should not check the plan for observability - we believe it has been checked and approved already */
            scheduler_log(ss, "Plan %d accepted: ra=%g dec=%g exposure=%g\n", id, ra, dec, exposure);
            send_gloria_request(ss, "/api/accept?id=%d", id);
            db_query(ss->scheduler->db, "INSERT INTO scheduler_targets (external_id, name, type, ra, dec, exposure, filter, status, time_created, uuid, params)"
                         " VALUES (%d, '%s', 'GLORIA', %g, %g, %g, '%s', get_scheduler_target_status_id('active'), favor2_timestamp(), '%s', 'min_altitude=>%g,max_moon_altitude=>%g,min_moon_distance=>%g'::hstore);",
                         id, name_fixed, ra, dec, exposure, filter, uuid, min_altitude, max_moon_altitude, min_moon_distance);

            if(name_fixed)
                free(name_fixed);
        } else if(!strcmp(status, "cancelled")){
            /* Cleanup the plan from scheduler database */
            db_query(ss->scheduler->db, "DELETE FROM scheduler_targets WHERE external_id = %d AND type = 'GLORIA';", id);

            /* Delete the plan */
            scheduler_log(ss, "Plan %d deleted\n", id);
            send_gloria_request(ss, "/api/delete?id=%d", id);
        }
    }
}

static void process_gloria_data(server_str *server, connection_str *connection, char *string, void *data)
{
    scheduler_server_str *ss = (scheduler_server_str *)data;
    static char *json_string = NULL;
    static int json_mode = FALSE;

    if(!strcmp(string, "Content-Type: application/json")){
        json_mode = TRUE;
        json_string = realloc(json_string, 0);
    } else if(json_mode){
        cJSON *json = NULL;

        add_to_string(&json_string, string);

        json = cJSON_Parse(json_string);

        if(json){
            json_mode = FALSE;

            if(cJSON_GetObjectItem(json, "plans"))
               process_gloria_JSON_plans(ss, cJSON_GetObjectItem(json, "plans"));

            cJSON_Delete(json);
        }
    }
}

static void get_pointing_suggestion(scheduler_server_str *ss, connection_str *conn, double duration)
{
    time_str time = time_current();
    double ra = 0;
    double dec = 0;
    double dsun = 0;
    double dmoon = 0;
    double dzenith = 0;
    scheduler_target_str *tgt = NULL;

    /* Update scheduler state from the DB */
    scheduler_restore_from_db(ss->scheduler, time);
    /* scheduler_update_coverage(ss->scheduler, time); */
    scheduler_update_interests(ss->scheduler);
    /* Update restrictions due to horizon, Moon and Sun */
    scheduler_update_restrictions(ss->scheduler, time);

    /* First try to schedule some of specific targets */
    tgt = scheduler_choose_target(ss->scheduler, time);

    if(tgt){
        /* Return object information */
        scheduler_distances_from_point(ss->scheduler, time, tgt->ra, tgt->dec, &dsun, &dmoon, &dzenith);
        server_connection_message(conn, "scheduler_pointing_suggestion type=%s name=%s id=%d ra=%g dec=%g exposure=%g repeat=%d filter=%s uuid=%s dsun=%g dmoon=%g dzenith=%g",
                                  tgt->type, tgt->name, tgt->id, tgt->ra, tgt->dec, tgt->exposure, tgt->repeat, tgt->filter, tgt->uuid, dsun, dmoon, dzenith);
        scheduler_target_delete(tgt);
    } else {
        /* No objects scheduled. Let's proceed with survey scheduling */
        scheduler_choose_survey_position(ss->scheduler, &ra, &dec);
        scheduler_distances_from_point(ss->scheduler, time, ra, dec, &dsun, &dmoon, &dzenith);
        server_connection_message(conn, "scheduler_pointing_suggestion type=survey name=none ra=%g dec=%g dsun=%g dmoon=%g dzenith=%g",
                                  ra, dec, dsun, dmoon, dzenith);

        /* Sync scheduler state with the DB */
        scheduler_dump_to_db(ss->scheduler);
    }
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    scheduler_server_str *ss = (scheduler_server_str *)data;
    command_str *command = command_parse(string);

    if(g_hash_table_lookup(ss->gloria_connections, connection)){
        process_gloria_data(server, connection, string, data);
        return;
    }
    /* server_default_line_read_hook(server, connection, string, data); */

    if(command_match(command, "exit") || command_match(command, "quit")){
        ss->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "get_scheduler_status")){
        get_scheduler_status(ss, connection);
    } else if(command_match(command, "get_pointing_suggestion")){
        double duration = 20*60;

        command_args(command,
                     "duration=%lf", &duration,
                     NULL);

        get_pointing_suggestion(ss, connection, duration);
    } else if(command_match(command, "set_position")){
        command_args(command,
                     "lat=%lf", &ss->scheduler->latitude,
                     "lon=%lf", &ss->scheduler->longitude,
                     NULL);

        scheduler_update_restrictions(ss->scheduler, time_current());
    } else if(command_match(command, "add_coverage")){
        double ra0 = 0;
        double dec0 = 0;
        double size_ra = 0;
        double size_dec = 0;
        double exposure = 0;
        int utime = 0;
        time_str time = time_current();
        char *timestamp = NULL;

        command_args(command,
                     "ra0=%lf", &ra0,
                     "dec0=%lf", &dec0,
                     "size_ra=%lf", &size_ra,
                     "size_dec=%lf", &size_dec,
                     "exposure=%lf", &exposure,
                     "time=%d", &utime,
                     NULL);

        time = time_from_unix(utime);
        timestamp = time_str_get_date_time(time);

        scheduler_restore_from_db(ss->scheduler, time);

        dprintf("Adding coverage at %g %g, %g x %g, %g seconds at %s\n",
                ra0, dec0, size_ra, size_dec, exposure, timestamp);

        scheduler_add_rectangle(ss->scheduler, PLANE_COVERAGE, ra0, dec0, size_ra, size_dec, exposure);

        ss->scheduler->time = time;

        scheduler_dump_to_db(ss->scheduler);

        free(timestamp);
    } else if(command_match(command, "target_observed") || command_match(command, "target_complete")){
        int id = 0;

        command_args(command, "id=%d", &id, NULL);

        if(id)
            scheduler_target_complete(ss->scheduler, id);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "schedule") || command_match(command, "target_create")){
        char *name = "unknown";
        char *type = "user";
        double ra = 0;
        double dec = 0;
        double exposure = 50.0;
        char *filter = "Clear";

        command_args(command,
                     "name=%s", &name,
                     "type=%s", &type,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "exposure=%lf", &exposure,
                     "filter=%s", &filter,
                     NULL);

        scheduler_target_create(ss->scheduler, name, type, ra, dec, exposure, filter);

        server_connection_message(connection, "%s_done", command_name(command));
    } else if(command_match(command, "set_interest") || command_match(command, "add_interest")|| command_match(command, "update_interest")){
        char *name = NULL;
        double ra = 0;
        double dec = 0;
        double radius = 5;
        double weight = 1;
        int is_reschedule = FALSE;

        command_args(command,
                     "name=%s", &name,
                     "ra=%lf", &ra,
                     "dec=%lf", &dec,
                     "r=%lf", &radius,
                     "radius=%lf", &radius,
                     "weight=%lf", &weight,
                     "reschedule=%d", &is_reschedule,
                     NULL);

        if(name && radius > 0 && weight > 0){
            scheduler_interest_str *interest =  scheduler_find_interest(ss->scheduler, name);

            if(!interest)
                interest = scheduler_add_interest(ss->scheduler, name);

            if(interest){
                if(ra)
                    interest->ra = ra;
                if(dec)
                    interest->dec = dec;
                if(radius)
                    interest->radius = radius;
                if(weight)
                    interest->weight = weight;

                if(is_reschedule)
                    interest->is_reschedule = is_reschedule;

                if(interest->is_reschedule)
                    server_connection_message(ss->beholder_connection, "reschedule");
            }

            free(name);
        } else if(name)
            scheduler_delete_interest(ss->scheduler, name);
    } else if(command_match(command, "delete_interest")){
        char *name = NULL;

        command_args(command,
                     "name=%s", &name,
                     NULL);

        if(name)
            scheduler_delete_interest(ss->scheduler, name);
    } else if(command_match(command, "unknown_command")){
    } else
        server_connection_message(connection, "unknown_command %s", command_name(command));

    command_delete(command);
}

void scheduler_request_gloria_state_cb(server_str *server, int type, void *data)
{
    scheduler_server_str *ss = (scheduler_server_str *)data;

    /* We will try to prevent flooding the server with connections */
    if(g_hash_table_size(ss->gloria_connections) < 10)
        send_gloria_request(ss, "/api/list");
    else
        dprintf("Too many (%d) GLORIA connections\n", g_hash_table_size(ss->gloria_connections));

    server_add_timer(ss->server, 60, 0, scheduler_request_gloria_state_cb, ss);
}

static void connection_connected(server_str *server, connection_str *connection, void *data)
{
    scheduler_server_str *ss = (scheduler_server_str *)data;

    if(connection == ss->logger_connection){
        server_connection_message(connection, "iam id=scheduler");
    }

    server_default_connection_connected_hook(server, connection, NULL);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    scheduler_server_str *ss = (scheduler_server_str *)data;

    if(g_hash_table_lookup(ss->gloria_connections, connection))
        g_hash_table_remove(ss->gloria_connections, connection);

    server_default_connection_disconnected_hook(server, connection, NULL);
}

int main(int argc, char **argv)
{
    scheduler_server_str *ss = (scheduler_server_str *)malloc(sizeof(scheduler_server_str));
    int port = PORT_SCHEDULER;
    char *gloria_host = "localhost";
    int gloria_port = PORT_GLORIA;
    char *logger_host = "localhost";
    int logger_port = PORT_LOGGER;
    char *beholder_host = "localhost";
    int beholder_port = PORT_BEHOLDER;
    double resolution = 1;
    double latitude = 43.65722222;
    double longitude = -41.43235417;
    double fov = 30.0;

    parse_args(argc, argv,
               "port=%d", &port,
               "gloria_host=%s", &gloria_host,
               "gloria_port=%d", &gloria_port,
               "logger_host=%s", &logger_host,
               "logger_port=%d", &logger_port,
               "beholder_host=%s", &beholder_host,
               "beholder_port=%d", &beholder_port,
               "resolution=%lf", &resolution,
               "lat=%lf", &latitude,
               "lon=%lf", &longitude,
               "fov=%lf", &fov,
               NULL);

    check_license(argv[0]);

    /* Constructor */
    ss->server = server_create();
    server_listen(ss->server, "localhost", port);
    SERVER_SET_HOOK(ss->server, line_read_hook, process_command, ss);
    /* Set up custom network post-connection code */
    SERVER_SET_HOOK(ss->server, connection_connected_hook, connection_connected, ss);
    /* Set up custom network connection lost hook */
    SERVER_SET_HOOK(ss->server, connection_disconnected_hook, connection_disconnected, ss);

    ss->gloria_host = gloria_host;
    ss->gloria_port = gloria_port;
    ss->gloria_connections = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    server_add_timer(ss->server, 1, 0, scheduler_request_gloria_state_cb, ss);

    /* Add a dedicated logger connection */
    ss->logger_connection = server_add_connection(ss->server, logger_host, logger_port);
    ss->logger_connection->is_active = TRUE;
    server_connection_message(ss->logger_connection, "iam id=scheduler");

    /* Add beholder connection */
    ss->beholder_connection = server_add_connection(ss->server, beholder_host, beholder_port);
    ss->beholder_connection->is_active = TRUE;

    ss->scheduler = scheduler_create(resolution);
    ss->scheduler->latitude = latitude;
    ss->scheduler->longitude = longitude;
    ss->scheduler->dalpha = fov;
    ss->scheduler->ddelta = fov;

    /* Bootstrap current state of the scheduler */
    scheduler_restore_from_db(ss->scheduler, time_current());
    /* scheduler_update_coverage(ss->scheduler, time_current()); */
    scheduler_update_interests(ss->scheduler);
    /* Update restrictions, just in case */
    scheduler_update_restrictions(ss->scheduler, time_current());

    ss->is_quit = FALSE;

    /* Main event cycle */
    while(!ss->is_quit)
        server_cycle(ss->server, 10);

    /* Destructor */
    scheduler_delete(ss->scheduler);
    server_delete(ss->server);

    free(ss);

    return EXIT_SUCCESS;
}
