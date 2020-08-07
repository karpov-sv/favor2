#ifndef DB_H
#define DB_H

#include <libpq-fe.h>
#include <stdarg.h>

#include "time_str.h"

typedef struct {
    PGconn *conn;
    int initialized;
} db_str;

db_str *db_create(char *);
void db_delete(db_str *);

PGresult *db_query(db_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));
char *db_get_char(PGresult *, int , int );
double db_get_double(PGresult *, int , int );
int db_get_int(PGresult *, int , int );
time_str db_get_time_str(PGresult *res, int tup, int num);

#endif /* DB_H */
