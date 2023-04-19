
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <machinarium.h>
#include <odyssey.h>

od_file_lock_t od_get_execution_lock(char *prefix)
{
	char od_exec_lock_name[ODYSSEY_LOCK_MAXPATH - 4];
	if (prefix != NULL) {
		sprintf(od_exec_lock_name, "%s/%s:%d", prefix,
			ODYSSEY_LOCK_PREFIX, ODYSSEY_EXEC_LOCK_HASH);
	} else {
		sprintf(od_exec_lock_name, "%s/%s:%d", ODYSSEY_DEFAULT_LOCK_DIR,
			ODYSSEY_LOCK_PREFIX, ODYSSEY_EXEC_LOCK_HASH);
	}

	od_dbg_printf_on_dvl_lvl(1, "using exec lock %s\n", od_exec_lock_name);

	int fd = open(od_exec_lock_name, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG);

	if (fd == -1) {
		od_dbg_printf_on_dvl_lvl(
			1, "failed to get control lock file due error: %d",
			errno);
	}

	return fd;
}

od_file_lock_t od_get_control_lock(char *prefix)
{
	char od_control_lock_name[ODYSSEY_LOCK_MAXPATH];
	if (prefix != NULL) {
		sprintf(od_control_lock_name, "%s/%s:%d", prefix,
			ODYSSEY_LOCK_PREFIX, ODYSSEY_CTRL_LOCK_HASH);
	} else {
		sprintf(od_control_lock_name, "%s/%s:%d",
			ODYSSEY_DEFAULT_LOCK_DIR, ODYSSEY_LOCK_PREFIX,
			ODYSSEY_CTRL_LOCK_HASH);
	}
	od_dbg_printf_on_dvl_lvl(1, "using ctrl lock %s\n",
				 od_control_lock_name);

	int fd =
		open(od_control_lock_name, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG);

	if (fd == -1) {
		od_dbg_printf_on_dvl_lvl(
			1, "failed to get control lock file due error: %d",
			errno);
	}

	return fd;
}
