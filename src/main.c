#include "config.h"
#include "iec104.h"
#include "logging.h"
#include "point_store.h"
#include "process.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

/* 捕获退出信号，通知主循环停止并进行资源清理。 */
static void handle_signal(int signo)
{
    (void)signo;
    g_running = 0;
}

/* 安装 SIGINT/SIGTERM 处理函数，并忽略 SIGPIPE 防止网络断开导致进程退出。 */
static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);
    return 0;
}

/* 程序入口：加载配置、初始化日志和点表，然后启动 IEC104 主站采集循环。 */
int main(int argc, char **argv)
{
    app_config_t config;
    const char *config_path = "config/iec104_master.conf";
    int opt;
    int force_foreground = 0;
    int force_daemon = 0;
    int force_detail_debug = 0;
    point_store_t *store = NULL;
    int rc = EXIT_FAILURE;

    config_set_defaults(&config);

    while ((opt = getopt(argc, argv, "c:dfvh")) != -1) {
        switch (opt) {
        case 'c':
            config_path = optarg;
            break;
        case 'd':
            force_daemon = 1;
            break;
        case 'f':
            force_foreground = 1;
            break;
        case 'v':
            force_detail_debug = 1;
            break;
        case 'h':
            config_print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            config_print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (config_load(&config, config_path) != 0) {
        return EXIT_FAILURE;
    }

    if (force_foreground) {
        config.daemonize = 0;
    } else if (force_daemon) {
        config.daemonize = 1;
    }

    if (force_detail_debug) {
        config.debug_level = APP_DEBUG_DETAIL;
    }

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "failed to install signal handlers: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (config.daemonize && process_daemonize() != 0) {
        fprintf(stderr, "failed to daemonize: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (process_pidfile_acquire(config.pid_file) != 0) {
        return EXIT_FAILURE;
    }

    if (log_init(config.log_file, config.debug_log_file, config.daemonize, config.debug_level) != 0) {
        process_pidfile_release();
        return EXIT_FAILURE;
    }

    store = point_store_create();
    if (store == NULL) {
        log_write(LOG_LEVEL_ERROR, "failed to create point store");
        goto cleanup;
    }

    log_write(LOG_LEVEL_INFO,
              "iec104-master started slave=%s:%u ca=%u daemon=%d debug_level=%d",
              config.slave_host,
              config.slave_port,
              config.common_address,
              config.daemonize,
              config.debug_level);

    if (iec104_master_run(&config, store, &g_running) == 0) {
        rc = EXIT_SUCCESS;
    }

cleanup:
    log_write(LOG_LEVEL_INFO, "iec104-master stopped");
    point_store_destroy(store);
    log_close();
    process_pidfile_release();
    return rc;
}
