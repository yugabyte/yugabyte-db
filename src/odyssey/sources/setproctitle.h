#ifndef ODYSSEY_SETPROCTITLE_H
#define ODYSSEY_SETPROCTITLE_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define OD_MAX_PROC_TITLE_LEN 256

od_retcode_t od_setproctitlef(char **argv_ptr, char *fmt, ...);

#endif /* ODYSSEY_SETPROCTITLE_H */
