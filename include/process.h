#ifndef IEC104_MASTER_PROCESS_H
#define IEC104_MASTER_PROCESS_H

/* 将进程转换为守护进程。 */
int process_daemonize(void);

/* 创建并锁定 PID 文件。 */
int process_pidfile_acquire(const char *pid_file);

/* 释放并删除 PID 文件。 */
void process_pidfile_release(void);

#endif
