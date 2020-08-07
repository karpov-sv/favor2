#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

#include "script.h"

void process_message(char *message, void *data)
{
    script_str *script = (script_str *)data;

    dprintf("Message: %s\n", message);

    script_message(script, "%s_done", message);
}

int main(int argc, char **argv)
{
    script_str *script = script_create();

    script->message_callback = process_message;
    script->message_callback_data = script;

    script_command(script, "package.path = 'lua/?.lua;' .. package.path");
    script_load(script, "lua/beholder_global_fsm.lua");
    script_load(script, "lua/beholder_global.lua");


    script_message(script, "night_on");
    script_message(script, "weather_good");
    script_message(script, "weather_bad");
    script_message(script, "night_off");
    script_message(script, "weather_good");

    script_delete(script);

    return EXIT_SUCCESS;
}
