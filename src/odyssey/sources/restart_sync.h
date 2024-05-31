#ifndef ODYSSEY_SEMA_H
#define ODYSSEY_SEMA_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

/*
 * A named semaphore is identified by a name of the form /somename;
 * that is, a null-terminated string of up to NAME_MAX-4 (i.e., 251)
 * characters consisting of an initial slash, followed by one or more
 * characters, none of which are slashes.
 * Two processes can operate on the same named semaphore by
 * passing the same name to sem_open.
 * */

#define __odyssey_internal_ch_to_int(ch) \
	(unsigned int)(ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch - 'a')

#define ODYSSEY_CTRL_LOCK_HASH                        \
	(((__odyssey_internal_ch_to_int('O') << 6) |  \
	  (__odyssey_internal_ch_to_int('d') << 5) |  \
	  (__odyssey_internal_ch_to_int('y') << 4) |  \
	  (__odyssey_internal_ch_to_int('s') << 3) |  \
	  (__odyssey_internal_ch_to_int('s') << 2) |  \
	  (__odyssey_internal_ch_to_int('e') << 1) |  \
	  (__odyssey_internal_ch_to_int('y') << 0)) ^ \
	 1729u) //  Hardy–Ramanujan number 1 ^ 3 + 12 ^ 3 = 9 ^ 3 + 10 ^ 3 = 1729

#define ODYSSEY_EXEC_LOCK_HASH                        \
	(((__odyssey_internal_ch_to_int('P') << 5) |  \
	  (__odyssey_internal_ch_to_int('o') << 4) |  \
	  (__odyssey_internal_ch_to_int('o') << 3) |  \
	  (__odyssey_internal_ch_to_int('l') << 2) |  \
	  (__odyssey_internal_ch_to_int('e') << 1) |  \
	  (__odyssey_internal_ch_to_int('r') << 0)) | \
	 1729u) //  Hardy–Ramanujan number 1 ^ 3 + 12 ^ 3 = 9 ^ 3 + 10 ^ 3 = 1729

#define ODYSSEY_DEFAULT_LOCK_DIR "/tmp"
#define ODYSSEY_LOCK_PREFIX "odyssey-restart-lock"
#define ODYSSEY_LOCK_MAXPATH PATH_MAX

typedef int od_file_lock_t;

extern od_file_lock_t od_get_control_lock(char *prefix);

extern od_file_lock_t od_get_execution_lock(char *prefix);

#endif /* ODYSSEY_SEMA_H */
