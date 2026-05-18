#ifndef IEC104_MASTER_PROCESS_H
#define IEC104_MASTER_PROCESS_H

int process_daemonize(void);
int process_pidfile_acquire(const char *pid_file);
void process_pidfile_release(void);

#endif
