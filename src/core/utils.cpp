#include "utils.h"

#include <stdio.h>

void LogMessage(const char* level, const char* fmt, ...)
{
    va_list args;
    fprintf(stderr, "%s ", level);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void LogFlush()
{
    fflush(stdout);
}

