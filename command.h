#ifndef COMMAND_H
#define COMMAND_H

#include <stdarg.h>

typedef struct {
    char *name;
    char *value;
    int is_used;
} command_token_str;

typedef struct {
    char *string;

    int length;

    command_token_str *tokens;
} command_str;

command_str *command_parse(const char *);
void command_delete(command_str *);
char *command_name(command_str *);
char *command_params_n(command_str *, int );
char *command_params(command_str *);
char *command_params_with_prefix(command_str *, const char *, ...) __attribute__ ((format (printf, 2, 3)));
char *command_params_as_hstore(command_str *);
char *command_params_as_lua(command_str *);
int command_match(command_str *, const char *);
void command_args(command_str *, ...);

#endif /* COMMAND_H */
