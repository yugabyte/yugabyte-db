
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <machinarium.h>
#include <odyssey.h>

int od_daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid > 0) {
		/* shutdown parent */
		_exit(0);
	}
	setsid();
	int fd;
	fd = open("/dev/null", O_RDWR);
	if (fd < 0)
		return -1;
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2) {
		close(fd);
	}
	return 0;
}
