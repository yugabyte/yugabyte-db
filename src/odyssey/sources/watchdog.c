/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

void od_watchdog_worker(void *arg)
{
	od_instance_t *instance = arg;

	int fd_ctrl = od_get_control_lock(instance->config.locks_dir);
	if (fd_ctrl == -1) {
		od_error(
			&instance->logger, "watchdog", NULL, NULL,
			"failed to create ctrl lock file in %s (errno: %d) try to "
			"specify another locks dir or disable online restart feature",
			instance->config.locks_dir == NULL ?
				ODYSSEY_DEFAULT_LOCK_DIR :
				instance->config.locks_dir,
			errno);

		if (instance->config.graceful_die_on_errors) {
			kill(instance->pid.pid, OD_SIG_GRACEFUL_SHUTDOWN);
		} else {
			kill(instance->pid.pid, SIGKILL);
		}
		return;
	}
	int fd_exec = od_get_execution_lock(instance->config.locks_dir);
	if (fd_exec == -1) {
		od_log(&instance->logger, "watchdog", NULL, NULL,
		       "failed to create exec lock file in %s (errno: %d) try to "
		       "specify another locks dir or disable online restart feature",
		       instance->config.locks_dir == NULL ?
			       ODYSSEY_DEFAULT_LOCK_DIR :
			       instance->config.locks_dir,
		       errno);
		if (instance->config.graceful_die_on_errors) {
			kill(instance->pid.pid, OD_SIG_GRACEFUL_SHUTDOWN);
		} else {
			kill(instance->pid.pid, SIGKILL);
		}
		close(fd_ctrl);
		return;
	}

	od_dbg_printf_on_dvl_lvl(1, "try to acquire ctrl lock %d\n", fd_ctrl);
	while (1) {
		if (flock(fd_ctrl, LOCK_EX | LOCK_NB) == 0) {
			od_dbg_printf_on_dvl_lvl(1, "acquire ctrl lock ok %d\n",
						 fd_ctrl);
			break;
		}
		machine_sleep(ODYSSEY_WATCHDOG_ITER_INTERVAL);
	}

	od_dbg_printf_on_dvl_lvl(1, "try to acquire exec lock %d\n", fd_exec);
	while (1) {
		if (flock(fd_exec, LOCK_EX | LOCK_NB) == 0) {
			od_dbg_printf_on_dvl_lvl(1, "acquire exec lock ok %d\n",
						 fd_exec);
			break;
		}
		machine_sleep(ODYSSEY_WATCHDOG_ITER_INTERVAL);
	}

	flock(fd_ctrl, LOCK_UN | LOCK_NB);
	while (1) {
		if (flock(fd_ctrl, LOCK_EX | LOCK_NB) == -1) {
			od_dbg_printf_on_dvl_lvl(1, "release exec lock %d\n",
						 fd_exec);
			break;
		}

		flock(fd_ctrl, LOCK_UN | LOCK_NB);

		od_dbg_printf_on_dvl_lvl(1, "watchdog worker sleep for %d ms\n",
					 ODYSSEY_WATCHDOG_ITER_INTERVAL);
		machine_sleep(ODYSSEY_WATCHDOG_ITER_INTERVAL);
	}
	flock(fd_exec, LOCK_UN | LOCK_NB);

	/* request our own process to shutdown  */
	kill(instance->pid.pid, OD_SIG_GRACEFUL_SHUTDOWN);
	return;
}

od_retcode_t od_watchdog_invoke(od_system_t *system)
{
	od_instance_t *instance = system->global->instance;

	int64_t id = machine_create("watchdog", od_watchdog_worker, instance);
	if (id == -1) {
		od_error(&instance->logger, "cron", NULL, NULL,
			 "failed to start watchdog coroutine");
		return NOT_OK_RESPONSE;
	}
	return OK_RESPONSE;
}
