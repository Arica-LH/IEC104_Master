#ifndef IEC104_MASTER_LOGGING_H
#define IEC104_MASTER_LOGGING_H

typedef enum log_level {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

/* 初始化日志输出，debug_level 控制 off/info/detail 三种打印等级，DEBUG 日志单独写入 debug_log_file。 */
int log_init(const char *log_file, const char *debug_log_file, int daemon_mode, int debug_level);

/* 关闭日志输出。 */
void log_close(void);

/* 输出一条带等级和时间戳的日志。 */
void log_write(log_level_t level, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#endif
