#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>

#include "utils.h"
#include "db.h"

//#define DB_DEBUG 1

void db_check_connection(db_str *);

db_str *db_create(char *options)
{
    db_str *db = (db_str *)malloc(sizeof(db_str));

    db->conn = PQconnectdb(options);

    if(PQstatus(db->conn) == CONNECTION_BAD){
        /* We will try to reconnect later */
        dprintf("DB error: %s\n", PQerrorMessage(db->conn));
    }

    db->initialized = FALSE;

    /* Initialize parameters */
    db_check_connection(db);

    return db;
}

void db_delete(db_str *db)
{
    if(!db)
	return;

    PQfinish(db->conn);

    free(db);
}

void db_check_connection(db_str *db)
{
    /* Check connection status first */
    if(db && PQstatus(db->conn) == CONNECTION_BAD){
        /* Try to reestablish the connection */
        PQreset(db->conn);
        db->initialized = FALSE;
    }

    /* Initialize connection parameters, if necessary */
    if(db && PQstatus(db->conn) == CONNECTION_OK && !db->initialized){
	PGresult *res = PQexec(db->conn, "set datestyle='DMY';");

        if(PQresultStatus(res) == PGRES_COMMAND_OK)
            db->initialized = TRUE;

        PQclear(res);
    }
}

PGresult *db_query(db_str *db, const char *template, ...)
{
    va_list ap;
    char *query;

    va_start (ap, template);
    vasprintf ((char**)&query, template, ap);
    va_end (ap);

#ifdef DB_DEBUG
    ddprintf("DB Query: %s\n", query);
#endif

    db_check_connection(db);

    /* Perform the query only if already connected */
    if(db && PQstatus(db->conn) == CONNECTION_OK){
	PGresult *res = PQexec(db->conn, query);

        free(query);

	switch(PQresultStatus(res)){

	    /* Empty response */
	case PGRES_COMMAND_OK:
	    PQclear(res);
	    return NULL;

	    /* Normal response */
	case PGRES_TUPLES_OK:
	    return res;

	    /* Error */
	default:
	    dprintf("DB error: %s\n", PQresStatus(PQresultStatus(res)));
            dprintf("%s\n\n", PQresultErrorMessage(res));
        }
    } else
        free(query);

    return NULL;
}

char *db_get_char(PGresult *res, int tup, int num)
{
    return make_string("%s", PQgetvalue(res, tup, num));
}

double db_get_double(PGresult *res, int tup, int num)
{
    char *string = PQgetvalue(res, tup, num);
    double value = 0;

    sscanf(string, "%lf", &value);

    return value;
}

int db_get_int(PGresult *res, int tup, int num)
{
    char *string = PQgetvalue(res, tup, num);
    int value = 0;

    sscanf(string, "%d", &value);

    return value;
}


time_str db_get_time_str(PGresult *res, int tup, int num)
{
    return time_str_from_date_time(PQgetvalue(res, tup, num));
}
