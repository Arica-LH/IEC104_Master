#ifndef IEC104_MASTER_LOGGING_H
#define IEC104_MASTER_LOGGING_H

typedef enum log_level {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

int log_init(const char *log_file, int daemon_mode, int debug_level);
void log_close(void);
void log_write(log_level_t level, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#endif
