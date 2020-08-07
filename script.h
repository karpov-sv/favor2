#ifndef SCRIPT_H
#define SCRIPT_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

typedef struct script_str {
    lua_State *l;

    /* send_message */
    void (*message_callback)(char *, void *);
    void *message_callback_data;

    /* send_message_to */
    void (*message_to_callback)(char *, char *, void *);
    void *message_to_callback_data;
} script_str;

script_str *script_create();
void script_delete(script_str *);
/* Load (and run) the script from file */
int script_load(script_str *, char *);
/* Send raw Lua command */
int script_command(script_str *, const char *, ...);
/* Get possible returns from command */
char *script_get_string_result(script_str *);
double script_get_number_result(script_str *);
int script_get_int_result(script_str *);
int script_get_boolean_result(script_str *);

/* Send message to the script (to get_message function, namely) */
void script_message(script_str *, const char *, ...);
void script_message_from(script_str *, char *, const char *, ...);

/* Push variable into Lua globals */
void script_set_boolean(script_str *, char *, int );
void script_set_number(script_str *, char *, double );
void script_set_string(script_str *, char *, char *);

/* Get variable from Lua globals */
char *script_get_string(script_str *, char *);
double script_get_number(script_str *, char *);
int script_get_int(script_str *, char *);
int script_get_boolean(script_str *, char *);

#endif /* SCRIPT_H */
