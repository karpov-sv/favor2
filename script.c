#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

#include "script.h"

/* Store the pointer in LUA registry */
static void lua_store_ptr(lua_State *l, char *key, void *ptr)
{
    lua_pushlightuserdata(l, (void *)key);
    lua_pushlightuserdata(l, (void *)ptr);
    /* registry[key] = ptr */
    lua_settable(l, LUA_REGISTRYINDEX);
}

/* Retrieve the pointer from LUA registry */
static void *lua_retrieve_ptr(lua_State *l, char *key)
{
    lua_pushlightuserdata(l, (void *)key);
    lua_gettable(l, LUA_REGISTRYINDEX);

    return (void *)lua_topointer(l, -1);
}

static int lua_send_message(lua_State *l)
{
    script_str *script = lua_retrieve_ptr(l, "script");
    char *msg = (char *)luaL_checkstring(l, 1);

    if(script->message_callback)
        script->message_callback(msg, script->message_callback_data);

    return 0;
}

static int lua_send_message_to(lua_State *l)
{
    script_str *script = lua_retrieve_ptr(l, "script");
    char *target = (char *)luaL_checkstring(l, 1);
    char *msg = (char *)luaL_checkstring(l, 2);

    if(script->message_to_callback)
        script->message_to_callback(target, msg, script->message_callback_data);

    return 0;
}

static int lua_dprint(lua_State *l)
{
    const char *msg = luaL_checkstring(l, 1);

    dprintf("%s\n", msg);

    return 0;
}

script_str *script_create()
{
    script_str *script = (script_str *)malloc(sizeof(script_str));

    script->l = luaL_newstate();
    luaL_openlibs(script->l);
    lua_checkstack(script->l, 1000); /* ??? */

    script->message_callback = NULL;
    script->message_callback_data = NULL;

    script->message_to_callback = NULL;
    script->message_to_callback_data = NULL;

    /* send_message */
    lua_pushcfunction(script->l, lua_send_message);
    lua_setglobal(script->l, "send_message");

    /* send_message_to */
    lua_pushcfunction(script->l, lua_send_message_to);
    lua_setglobal(script->l, "send_message_to");

    /* dprint */
    lua_pushcfunction(script->l, lua_dprint);
    lua_setglobal(script->l, "dprint");

    lua_store_ptr(script->l, "script", script);

    script_command(script, "package.path = 'lua/?.lua;' .. package.path");

    return script;
}

void script_delete(script_str *script)
{
    lua_close(script->l);

    free(script);
}

int script_load(script_str *script, char *filename)
{
    int err = luaL_dofile(script->l, filename);

    if(err)
        dprintf("LUA error: %s\n", lua_tostring(script->l, -1));

    return err ? FALSE : TRUE;
}

int script_command(script_str *script, const char *template, ...)
{
    va_list ap;
    char *buffer = NULL;
    int err = 0;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    err = luaL_dostring(script->l, buffer);

    if(err)
        dprintf("Error: %s\n", lua_tostring(script->l, -1));

    free(buffer);

    return err ? FALSE : TRUE;
}

char *script_get_string_result(script_str *script)
{
    char *value = NULL;

    value = make_string("%s", lua_tostring(script->l, -1));
    lua_pop(script->l, -1);

    return value;
}

double script_get_number_result(script_str *script)
{
    double value = 0;

    value = lua_tonumber(script->l, -1);
    lua_pop(script->l, -1);

    return value;
}

int script_get_int_result(script_str *script)
{
    int value = 0;

    value = lua_tointeger(script->l, -1);
    lua_pop(script->l, -1);

    return value;
}

int script_get_boolean_result(script_str *script)
{
    int value = 0;

    value = lua_toboolean(script->l, -1);
    lua_pop(script->l, -1);

    return value;
}

void script_message(script_str *script, const char *template, ...)
{
    va_list ap;
    char *buffer = NULL;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    script_command(script, "get_message('%s')", buffer);

    free(buffer);
}

void script_message_from(script_str *script, char *source, const char *template, ...)
{
    va_list ap;
    char *buffer = NULL;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    script_command(script, "get_message('%s', '%s')", buffer, source);

    free(buffer);
}

/* Push variable into Lua globals */
void script_set_boolean(script_str *script, char *name, int value)
{
    lua_pushboolean(script->l, value);
    lua_setglobal(script->l, name);
}

void script_set_number(script_str *script, char *name, double value)
{
    lua_pushnumber(script->l, value);
    lua_setglobal(script->l, name);
}

void script_set_string(script_str *script, char *name, char *value)
{
    lua_pushstring(script->l, value);
    lua_setglobal(script->l, name);
}

char *script_get_string(script_str *script, char *name)
{
    char *value = NULL;

    lua_getglobal(script->l, name);
    value = make_string("%s", lua_tostring(script->l, -1));
    lua_pop(script->l, -1);

    return value;
}

double script_get_number(script_str *script, char *name)
{
    double value = 0;

    lua_getglobal(script->l, name);
    value = lua_tonumber(script->l, -1);
    lua_pop(script->l, -1);

    return value;
}

int script_get_int(script_str *script, char *name)
{
    int value = 0;

    lua_getglobal(script->l, name);
    value = lua_tointeger(script->l, -1);
    lua_pop(script->l, -1);

    return value;
}

int script_get_boolean(script_str *script, char *name)
{
    int value = 0;

    lua_getglobal(script->l, name);
    value = lua_toboolean(script->l, -1);
    lua_pop(script->l, -1);

    return value;
}
