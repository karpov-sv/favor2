#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "utils.h"

#include "command.h"

static void command_push_arg(command_str *command, char *name, char *value)
{
    command->tokens = realloc(command->tokens, sizeof(command_token_str)*(command->length + 1));

    command->tokens[command->length].name = name ? make_string("%s", name) : NULL;
    command->tokens[command->length].value = value ? make_string("%s", value) : NULL;
    command->tokens[command->length].is_used = FALSE;

    command->length ++;
}

command_str *command_parse(const char *string)
{
    command_str *command = (command_str *)malloc(sizeof(command_str));

    command->string = make_string("%s", string);

    command->length = 0;
    command->tokens = NULL;

    /* Parse string */
    if(*string){
        char *start = command->string;
        char *pos = command->string;
        int state = 0;

        /* TODO: add support for backslashed chars (whitespace, quote etc) */

        /* States:
           0 - initial
           1 - whitespace
           2 - token
           3 - quote
           4 - double quote
           100 - final
        */

        do {
            if(isalnum(*pos) || ispunct(*pos) || *pos == '"' || *pos == '\'')
                switch(state){
                case 0:
                case 1:
                    start = pos;
                    state = 2;
                case 2:
                    if(*pos == '\'')
                        state = 3;
                    else if(*pos == '"')
                        state = 4;
                    break;
                case 3:
                    if(*pos == '\'')
                        state = 2;
                    break;
                case 4:
                    if(*pos == '"')
                        state = 2;
                    break;
                default:
                    break;
                }
            else if(isspace(*pos) || *pos == '\0')
                switch(state){
                case 0:
                    state = 1;
                    break;
                case 2:
                    {
                        /* Token found */
                        char *token = strndup(start, pos - start);
                        char *eqpos = strchr(token, '=');

                        if(eqpos){
                            *eqpos = '\0';
                            command_push_arg(command, token, eqpos + 1);
                        } else
                            command_push_arg(command, NULL, token);

                        free(token);
                    }

                    state = 1;
                    break;
                default:
                    break;
                }

            if(*pos == '\0')
                state = 100;
            pos ++;
        } while(state != 100);
    }

    return command;
}

void command_delete(command_str *command)
{
    int d;

    if(command->string)
        free(command->string);

    for(d = 0; d < command->length; d++){
        if(command->tokens[d].name)
            free(command->tokens[d].name);
        if(command->tokens[d].value)
            free(command->tokens[d].value);
    }

    if(command->tokens)
        free(command->tokens);

    free(command);
}

char *command_name(command_str *command)
{
    if(command->length && !command->tokens[0].name && command->tokens[0].value)
        return command->tokens[0].value;
    else
        return NULL;
}

char *command_params_n(command_str *command, int skip)
{
    char *string = NULL;
    int d;

    if(skip + 1 < command->length)
        for(d = skip + 1; d < command->length; d++){
            if(string)
                add_to_string(&string, " ");

            if(command->tokens[d].name)
                add_to_string(&string, "%s=%s", command->tokens[d].name, command->tokens[d].value);
            else if(command->tokens[d].value)
                add_to_string(&string, "%s", command->tokens[d].value);
        }

    return string;
}

char *command_params(command_str *command)
{
    return command_params_n(command, 0);
}

char *command_params_with_prefix(command_str *command, const char *template, ...)
{
    char *string = NULL;
    int d;
    va_list ap;
    char *buffer = NULL;

    va_start(ap, template);
    vasprintf((char**)&buffer, template, ap);
    va_end(ap);

    for(d = 1; d < command->length; d++){
        if(string)
            add_to_string(&string, " ");

        if(command->tokens[d].name)
            add_to_string(&string, "%s_%s=%s", buffer, command->tokens[d].name, command->tokens[d].value);
        else if(command->tokens[d].value)
            add_to_string(&string, "%s_%s", buffer, command->tokens[d].value);
    }

    if(buffer)
        free(buffer);

    return string;
}

char *command_params_as_hstore(command_str *command)
{
    char *string = NULL;
    char *hstring = NULL;

    int d;

    for(d = 1; d < command->length; d++){
        if(string)
            add_to_string(&string, ", ");

        if(command->tokens[d].name)
            add_to_string(&string, "\"%s\" => \"%s\"", command->tokens[d].name, command->tokens[d].value);
    }

    hstring = make_string("'%s'::hstore", string);

    free(string);

    return hstring;
}

char *command_params_as_lua(command_str *command)
{
    char *string = NULL;
    char *hstring = NULL;

    int d;

    for(d = 1; d < command->length; d++){
        if(string)
            add_to_string(&string, ", ");

        if(command->tokens[d].name)
            add_to_string(&string, "[\"%s\"] = \"%s\"", command->tokens[d].name, command->tokens[d].value);
    }

    hstring = make_string("{%s}", string);

    free(string);

    return hstring;
}

int command_match(command_str *command, const char *cmdname)
{
    int result = FALSE;

    /* For a well-formed string, the command itself is in first token' value.
       At the same time, its name=NULL */
    if(cmdname && *cmdname && command->length &&
       !command->tokens[0].name && strcmp(cmdname, command->tokens[0].value) == 0)
        result = TRUE;

    return result;
}

/* FIXME: You should free() the strings you got from command_args(), and should
   not initialize it to a non-null string beforehand! (if it is static, your
   free() will segfault, if dynamic - the memory will be lost) */
void command_args(command_str *command, ...)
{
    va_list l;
    char *pattern;
    int *arg_used = NULL;
    int nargs = 0;
    int pass = 0;
    int d;

    /* Reset used flag for tokens */
    for(d = 0; d < command->length; d++)
        command->tokens[d].is_used = FALSE;

    /* We can't use a closure here, as va_* calls work in top-level func only */
make_pass:

    va_start(l, command);

    while((pattern = va_arg(l, char*))){
        void *pointer = va_arg(l, void*);
        char *eqpos = strchr(pattern, '=');
        char *name = NULL;
        char *format = NULL;

        if(eqpos){
            name = strndup(pattern, eqpos - pattern);
            format = make_string("%s", eqpos + 1);
        } else
            format = make_string("%s", pattern);

        if(pass == 0){
            arg_used = realloc(arg_used, sizeof(int)*(nargs + 1));
            arg_used[nargs] = FALSE;
        }

        for(d = 1; d < command->length; d++){
            if(!command->tokens[d].is_used && !arg_used[nargs] &&
               ((pass == 1 && !command->tokens[d].name) ||
                (command->tokens[d].name && strcmp(command->tokens[d].name, name) == 0))){
                if(strcmp(format, "%s") == 0){
                    /* Special handling of strings */
                    char *string = command->tokens[d].value;
                    int length = strlen(string);
                    char *tmp = NULL;

                    /* Remove quotes around, if any */
                    if((string[0] == '"' || string[0] == '\'') &&
                       (string[length - 1] == '"' || string[length - 1] == '\''))
                        tmp = strndup(string + 1, length - 2);
                    else
                        tmp = make_string("%s", string);

                    *(char **)pointer = tmp;
                } else
                    sscanf(command->tokens[d].value, format, pointer);

                command->tokens[d].is_used = TRUE;
                arg_used[nargs] = TRUE;
                break;
            }
        }

        nargs ++;

        if(format)
            free(format);
        if(name)
            free(name);
    }

    va_end(l);

    /* On first pass, we collected all named arguments. On secons, we'll match
       all the unnamed ones that left using their positions */
    if(pass == 0){
        pass ++;
        nargs = 0;
        goto make_pass;
    }

    if(arg_used)
        free(arg_used);
}
