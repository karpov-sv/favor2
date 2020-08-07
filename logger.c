#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>

#include "utils.h"

#include "db.h"
#include "ports.h"
#include "server.h"
#include "command.h"
#include "time_str.h"

typedef struct {
    server_str *server;

    GHashTable *ids;

    db_str *db;
    char *table;

    int is_quit;
} logger_str;

static void logger_log(logger_str *logger, char *id, char *text)
{
    char *timestamp = time_str_get_date_time(time_current());

    dprintf("%s\t%s\t%s\n", timestamp, id, text);

    /* We will not log to DB anonymous messages */
    if(id && *id && text && *text){
        PGresult *res = db_query(logger->db, "INSERT INTO %s (time, id, text) VALUES ('%s', '%s', '%s');",
                                 logger->table, timestamp, id, text);

        PQclear(res);
    }

    free(timestamp);
}

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    logger_str *logger = (logger_str *)data;
    command_str *command = command_parse(string);

    /* server_default_line_read_hook(server, connection, string, data); */

    if(command_match(command, "exit") || command_match(command, "quit")){
        logger->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "iam")){
        char *id = NULL;

        command_args(command, "id=%s", &id, NULL);
        /* Store the ID in hash table to find it using the connection */
        g_hash_table_insert(logger->ids, connection, make_string("%s", id));
    } else if(command_match(command, "log")){
        char *id = (char *)g_hash_table_lookup(logger->ids, connection);
        char tmp[1025];
        char *text = NULL;
        int pos = 0;

        tmp[0] = '\0';

        if(sscanf(string, "log id=%1024s%n", tmp, &pos)){
            text = string + pos;
            id = tmp;
        } else if(sscanf(string, "log %n%1024s", &pos, tmp)){
            text = string + pos;
        }

        while(text && *text && isspace(*text))
            text++;

        if(text && *text)
            logger_log(logger, id, text);
    } else if(command_match(command, "unknown_command")){
    } else {
        /* We will treat each and every other string as a message to be logged */
        char *id = (char *)g_hash_table_lookup(logger->ids, connection);

        logger_log(logger, id, string);
    }

    command_delete(command);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    logger_str *logger = (logger_str *)data;
    char *id = (char *)g_hash_table_lookup(logger->ids, connection);

    if(id){
        dprintf("Disconnected client with id: %s\n", id);

        g_hash_table_remove(logger->ids, connection);
    }
}

int main(int argc, char **argv)
{
    logger_str *logger = (logger_str *)malloc(sizeof(logger_str));
    int port = PORT_LOGGER;
    char *table = "log";
    int is_clear = FALSE;

    parse_args(argc, argv,
               "port=%d", &port,
               "table=%s", &table,
               "-clear", &is_clear,
               NULL);

    logger->server = server_create();
    server_listen(logger->server, "localhost", port);

    SERVER_SET_HOOK(logger->server, line_read_hook, process_command, logger);
    SERVER_SET_HOOK(logger->server, connection_disconnected_hook, connection_disconnected, logger);

    logger->is_quit = FALSE;

    logger->ids = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free);

    logger->db = db_create("dbname=favor2");
    logger->table = table;

    if(is_clear)
        db_query(logger->db, "DELETE FROM %s;", table);

    while(!logger->is_quit)
        server_cycle(logger->server, 10);

    g_hash_table_destroy(logger->ids);

    server_delete(logger->server);

    db_delete(logger->db);

    free(logger);

    return EXIT_SUCCESS;
}
