#ifndef WEB_CONSOLE_H
#define WEB_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

typedef void (*WebConsoleTextHandler)(char *output, size_t output_size);
typedef bool (*WebConsoleCommandHandler)(const char *payload, int length);
typedef bool (*WebConsoleAuthHandler)(const char *username,
                                      const char *password);

bool WebConsole_Start(WebConsoleTextHandler status_handler,
                      WebConsoleTextHandler events_handler,
                      WebConsoleTextHandler audit_handler,
                      WebConsoleCommandHandler command_handler,
                      WebConsoleAuthHandler auth_handler);

#endif
