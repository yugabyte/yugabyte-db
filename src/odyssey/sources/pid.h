#ifndef ODYSSEY_PID_H
#define ODYSSEY_PID_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_pid od_pid_t;

struct od_pid {
	pid_t pid;
	char pid_sz[16];
	int pid_len;
};

void od_pid_init(od_pid_t *);
int od_pid_create(od_pid_t *, char *);
int od_pid_unlink(od_pid_t *, char *);

#define OD_SIG_LOG_ROTATE SIGUSR1
#define OD_SIG_GRACEFUL_SHUTDOWN SIGUSR2

#endif /* ODYSSEY_PID_H */
