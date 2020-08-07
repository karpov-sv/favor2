#include <unistd.h>
#include <stdlib.h>

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
} dbserver_str;

static void process_command(server_str *server, connection_str *connection, char *string, void *data)
{
    dbserver_str *dbserver = (dbserver_str *)data;
    command_str *command = command_parse(string);

    /* server_default_line_read_hook(server, connection, string, data); */

    if(command_match(command, "exit") || command_match(command, "quit")){
        dbserver->is_quit = TRUE;
        server_connection_message(connection, "exit_ok");
    } else if(command_match(command, "sql")){
        char *query = command_params(command);

        dprintf("SQL: %s\n", query);
        PQclear(db_query(dbserver->db, query));

        free(query);
    } else if(command_match(command, "unknown_command")){
    } else {
        dprintf("Unknown command: %s\n", string);
    }

    command_delete(command);
}

static void connection_disconnected(server_str *server, connection_str *connection, void *data)
{
    dbserver_str *dbserver = (dbserver_str *)data;
    char *id = (char *)g_hash_table_lookup(dbserver->ids, connection);

    if(id){
        dprintf("Disconnected client with id: %s\n", id);

        g_hash_table_remove(dbserver->ids, connection);
    }
}

int main(int argc, char **argv)
{
    dbserver_str *dbserver = (dbserver_str *)malloc(sizeof(dbserver_str));
    int port = PORT_DBSERVER;
    char *table = "log";
    int is_clear = FALSE;

    parse_args(argc, argv,
               "port=%d", &port,
               "table=%s", &table,
               "-clear", &is_clear,
               NULL);

    dbserver->server = server_create();
    server_listen(dbserver->server, "localhost", port);

    SERVER_SET_HOOK(dbserver->server, line_read_hook, process_command, dbserver);
    SERVER_SET_HOOK(dbserver->server, connection_disconnected_hook, connection_disconnected, dbserver);

    dbserver->is_quit = FALSE;

    dbserver->ids = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free);

    dbserver->db = db_create("dbname=favor2");
    dbserver->table = table;

    if(is_clear)
        db_query(dbserver->db, "DELETE FROM %s;", table);

    while(!dbserver->is_quit)
        server_cycle(dbserver->server, 10);

    g_hash_table_destroy(dbserver->ids);

    server_delete(dbserver->server);

    db_delete(dbserver->db);

    free(dbserver);

    return EXIT_SUCCESS;
}
