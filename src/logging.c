#include "logging.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *g_log_fp;
static int g_daemon_mode;
static int g_debug_level = 1;

static const char *level_name(log_level_t level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static log_level_t minimum_log_level(void)
{
    if (g_debug_level <= 0) {
        return LOG_LEVEL_WARN;
    }
    if (g_debug_level == 1) {
        return LOG_LEVEL_INFO;
    }
    return LOG_LEVEL_DEBUG;
}

int log_init(const char *log_file, int daemon_mode, int debug_level)
{
    g_daemon_mode = daemon_mode;
    g_debug_level = debug_level;

    if (log_file != NULL && log_file[0] != '\0') {
        g_log_fp = fopen(log_file, "a");
        if (g_log_fp == NULL) {
            fprintf(stderr, "failed to open log file %s: %s\n", log_file, strerror(errno));
            return -1;
        }
        setvbuf(g_log_fp, NULL, _IOLBF, 0);
    }

    return 0;
}

void log_close(void)
{
    if (g_log_fp != NULL) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

void log_write(log_level_t level, const char *fmt, ...)
{
    char time_buf[64];
    struct timespec ts;
    struct tm tm_value;
    FILE *targets[2];
    size_t target_count = 0;

    if (level < minimum_log_level()) {
        return;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_value);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_value);

    if (g_log_fp != NULL) {
        targets[target_count++] = g_log_fp;
    }
    if (!g_daemon_mode || g_log_fp == NULL) {
        targets[target_count++] = (level >= LOG_LEVEL_WARN) ? stderr : stdout;
    }

    for (size_t i = 0; i < target_count; ++i) {
        va_list args;

        fprintf(targets[i], "%s.%03ld [%s] ", time_buf, ts.tv_nsec / 1000000L, level_name(level));
        va_start(args, fmt);
        vfprintf(targets[i], fmt, args);
        va_end(args);
        fputc('\n', targets[i]);
        fflush(targets[i]);
    }
}
