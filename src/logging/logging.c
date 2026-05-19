#include "logging/logging.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static FILE *g_log_fp;
static FILE *g_debug_log_fp;
static int g_daemon_mode;
static int g_debug_level = 1;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 递归创建目录，确保日志文件所在目录存在。 */
static int mkdir_recursive(const char *dir)
{
    char path[4096];
    size_t len;

    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }

    snprintf(path, sizeof(path), "%s", dir);
    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }

    for (char *cursor = path + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *cursor = '/';
        }
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/* 根据日志文件路径创建父目录。 */
static int ensure_log_parent_dir(const char *file_path)
{
    char dir[4096];
    char *slash;

    if (file_path == NULL || file_path[0] == '\0') {
        return 0;
    }

    snprintf(dir, sizeof(dir), "%s", file_path);
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        return 0;
    }
    if (slash == dir) {
        return 0;
    }

    *slash = '\0';
    return mkdir_recursive(dir);
}

/* 将内部日志等级枚举转换为日志文本前缀。 */
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

/* 根据配置的 debug_level 计算当前允许输出的最低日志等级。 */
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

/* 初始化日志模块，打开普通日志和独立 DEBUG 日志文件，并记录前台或守护进程输出模式。 */
int log_init(const char *log_file, const char *debug_log_file, int daemon_mode, int debug_level)
{
    g_daemon_mode = daemon_mode;
    g_debug_level = debug_level;

    if (log_file != NULL && log_file[0] != '\0') {
        if (ensure_log_parent_dir(log_file) != 0) {
            fprintf(stderr, "failed to create log directory for %s: %s\n", log_file, strerror(errno));
            return -1;
        }
        g_log_fp = fopen(log_file, "a");
        if (g_log_fp == NULL) {
            fprintf(stderr, "failed to open log file %s: %s\n", log_file, strerror(errno));
            return -1;
        }
        setvbuf(g_log_fp, NULL, _IOLBF, 0);
    }

    if (debug_log_file != NULL && debug_log_file[0] != '\0') {
        if (ensure_log_parent_dir(debug_log_file) != 0) {
            fprintf(stderr, "failed to create debug log directory for %s: %s\n", debug_log_file, strerror(errno));
            if (g_log_fp != NULL) {
                fclose(g_log_fp);
                g_log_fp = NULL;
            }
            return -1;
        }
        g_debug_log_fp = fopen(debug_log_file, "a");
        if (g_debug_log_fp == NULL) {
            fprintf(stderr, "failed to open debug log file %s: %s\n", debug_log_file, strerror(errno));
            if (g_log_fp != NULL) {
                fclose(g_log_fp);
                g_log_fp = NULL;
            }
            return -1;
        }
        setvbuf(g_debug_log_fp, NULL, _IOLBF, 0);
    }

    return 0;
}

/* 关闭日志文件句柄，释放日志模块占用的资源。 */
void log_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_fp != NULL) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    if (g_debug_log_fp != NULL) {
        fclose(g_debug_log_fp);
        g_debug_log_fp = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

/* 按等级过滤并输出日志，DEBUG 写入独立文件，普通日志写入主日志文件。 */
void log_write(log_level_t level, const char *fmt, ...)
{
    char time_buf[64];
    struct timespec ts;
    struct tm tm_value;
    FILE *targets[2];
    size_t target_count = 0;

    pthread_mutex_lock(&g_log_mutex);

    if (level < minimum_log_level()) {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_value);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_value);

    if (level == LOG_LEVEL_DEBUG) {
        if (g_debug_log_fp != NULL) {
            targets[target_count++] = g_debug_log_fp;
        } else if (g_log_fp != NULL) {
            targets[target_count++] = g_log_fp;
        }
    } else if (g_log_fp != NULL) {
        targets[target_count++] = g_log_fp;
    }

    if (!g_daemon_mode || target_count == 0) {
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

    pthread_mutex_unlock(&g_log_mutex);
}
