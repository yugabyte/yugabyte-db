

#include "sighandler.h"
#include "system.h"

#include <sys/ipc.h>
#include <sys/shm.h>
#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

static inline od_retcode_t
od_system_gracefully_killer_invoke(od_system_t *system)
{
	od_instance_t *instance = system->global->instance;
	if (instance->shutdown_worker_id != INVALID_COROUTINE_ID) {
		return OK_RESPONSE;
	}
	int64_t mid;
	mid = machine_create("shutdowner", od_grac_shutdown_worker, system);
	if (mid == -1) {
		od_error(&instance->logger, "gracefully_killer", NULL, NULL,
			 "failed to invoke gracefully killer coroutine");
		return NOT_OK_RESPONSE;
	}
	instance->shutdown_worker_id = mid;

	return OK_RESPONSE;
}

static inline void od_system_cleanup(od_system_t *system)
{
	od_instance_t *instance = system->global->instance;
	od_list_t *i;

	od_list_foreach(&instance->config.listen, i)
	{
		od_config_listen_t *listen;
		listen = od_container_of(i, od_config_listen_t, link);
		if (listen->host)
			continue;
		/* remove unix socket files */
		char path[PATH_MAX];
		od_snprintf(path, sizeof(path), "%s/.s.PGSQL.%d",
			    instance->config.unix_socket_dir, listen->port);
		unlink(path);
	}
}

static inline void yb_stats_shmem_cleanup(od_instance_t *instance)
{
	char *stats_shm_key = getenv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME);
	if (stats_shm_key == NULL)
		return;

	// It is a good practice to detach the shared memory before deleting it, but not mandatory.
	// So continue execution irrespective of the error while detaching.
	if (instance->yb_stats != NULL && shmdt(instance->yb_stats) < 0) {
		od_error(
			&instance->logger, "stats shmem cleanup", NULL, NULL,
			"Got error while detaching the stats in the shared memory, %s",
			strerror(errno));
	}

	int shmid = shmget(atoi(stats_shm_key), 0, 0);
	if (shmid == -1) {
		od_error(
			&instance->logger, "stats shmem cleanup", NULL, NULL,
			"Unable to delete the shared memeory segment related with stats, %s",
			strerror(errno));
		return;
	}

	if (shmctl(shmid, IPC_RMID, 0) == -1) {
		od_error(
			&instance->logger, "stats shmem cleanup", NULL, NULL,
			"Unable to delete the shared memeory segment related with stats, %s",
			strerror(errno));
		return;
	}

	od_log(&instance->logger, "stats shmem cleanup", NULL, NULL,
	       "Deleted the shared memory segment with id %d", shmid);
}

od_attribute_noreturn() void od_system_shutdown(od_system_t *system,
						od_instance_t *instance)
{
	od_worker_pool_t *worker_pool;

	worker_pool = system->global->worker_pool;
	od_log(&instance->logger, "system", NULL, NULL,
	       "SIGINT received, shutting down");

	yb_stats_shmem_cleanup(instance);

	// lock here
	od_cron_stop(system->global->cron);

	od_worker_pool_stop(worker_pool);

	od_router_free(system->global->router);
	/* Prevent OpenSSL usage during deinitialization */
	od_worker_pool_wait();

	od_extention_free(&instance->logger, system->global->extentions);

	od_system_cleanup(system);

	/* stop machinaruim and free */
	od_instance_free(instance);
	exit(0);
}

void od_system_signal_handler(void *arg)
{
	od_system_t *system = arg;
	od_instance_t *instance = system->global->instance;

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, OD_SIG_LOG_ROTATE);
	sigaddset(&mask, OD_SIG_GRACEFUL_SHUTDOWN);

	sigset_t ignore_mask;
	sigemptyset(&ignore_mask);
	sigaddset(&mask, SIGPIPE);
	int rc;
	rc = machine_signal_init(&mask, &ignore_mask);
	if (rc == -1) {
		od_error(&instance->logger, "system", NULL, NULL,
			 "failed to init signal handler");
		return;
	}
	for (;;) {
		rc = machine_signal_wait(UINT32_MAX);
		if (rc == -1)
			break;
		switch (rc) {
		case SIGTERM:
		case SIGINT:
			od_system_shutdown(system, instance);
			break;
		case SIGHUP:
			od_log(&instance->logger, "system", NULL, NULL,
			       "SIGHUP received");
			od_system_config_reload(system);
			break;
		case OD_SIG_LOG_ROTATE:
			if (instance->config.log_file) {
				od_log(&instance->logger, "system", NULL, NULL,
				       "SIGUSR1 received, reopening log");
				rc = od_logger_reopen(
					&instance->logger,
					instance->config.log_file);
				if (rc == -1) {
					od_error(
						&instance->logger, "system",
						NULL, NULL,
						"failed to reopen log file '%s'",
						instance->config.log_file);
				}
			}
			break;
		case OD_SIG_GRACEFUL_SHUTDOWN:
			if (instance->config.enable_online_restart_feature ||
			    instance->config.graceful_die_on_errors) {
				od_log(&instance->logger, "system", NULL, NULL,
				       "SIG_GRACEFUL_SHUTDOWN received");
				od_system_gracefully_killer_invoke(system);
			} else {
				od_log(&instance->logger, "system", NULL, NULL,
				       "SIGUSR2 received, but online restart feature not "
				       "enabled, doing nothing");
			}
			break;
		}
	}
}
