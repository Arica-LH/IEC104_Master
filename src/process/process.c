#include "process/process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int g_pid_fd = -1;
static char g_pid_path[4096];

/* 对 PID 文件加锁或解锁，防止同一配置启动多个主站实例。 */
static int set_pidfile_lock(short lock_type)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = lock_type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    return fcntl(g_pid_fd, F_SETLK, &lock);
}

/* 将当前进程转为传统守护进程模式，脱离终端并重定向标准输入输出。 */
int process_daemonize(void)
{
    pid_t pid;
    int fd;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        return -1;
    }

    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    umask(027);
    if (chdir("/") != 0) {
        return -1;
    }

    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        return -1;
    }

    if (fd > STDERR_FILENO) {
        close(fd);
    }

    return 0;
}

/* 创建并锁定 PID 文件，写入当前进程号。 */
int process_pidfile_acquire(const char *pid_file)
{
    char pid_buf[64];

    if (pid_file == NULL || pid_file[0] == '\0') {
        return 0;
    }

    g_pid_fd = open(pid_file, O_RDWR | O_CREAT, 0644);
    if (g_pid_fd < 0) {
        fprintf(stderr, "failed to open pid file %s: %s\n", pid_file, strerror(errno));
        return -1;
    }

    if (set_pidfile_lock(F_WRLCK) != 0) {
        fprintf(stderr, "another iec104-master process is running, pid file: %s\n", pid_file);
        close(g_pid_fd);
        g_pid_fd = -1;
        return -1;
    }

    if (ftruncate(g_pid_fd, 0) != 0) {
        close(g_pid_fd);
        g_pid_fd = -1;
        return -1;
    }

    snprintf(pid_buf, sizeof(pid_buf), "%ld\n", (long)getpid());
    if (write(g_pid_fd, pid_buf, strlen(pid_buf)) < 0) {
        close(g_pid_fd);
        g_pid_fd = -1;
        return -1;
    }

    snprintf(g_pid_path, sizeof(g_pid_path), "%s", pid_file);
    return 0;
}

/* 释放 PID 文件锁并删除 PID 文件。 */
void process_pidfile_release(void)
{
    if (g_pid_fd >= 0) {
        set_pidfile_lock(F_UNLCK);
        close(g_pid_fd);
        g_pid_fd = -1;
    }

    if (g_pid_path[0] != '\0') {
        unlink(g_pid_path);
        g_pid_path[0] = '\0';
    }
}
