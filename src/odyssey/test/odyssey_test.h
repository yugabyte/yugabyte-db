#ifndef ODYSSEY_TEST_H
#define ODYSSEY_TEST_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
/* for hba tests */
#include <arpa/inet.h>

#ifndef YB_SUPPORT_FOUND
#define odyssey_test(function)                                                \
	do {                                                                  \
		struct timeval tv_start, tv_stop;                             \
		fprintf(stdout, "%s: ", #function);                           \
		fflush(stdout);                                               \
		gettimeofday(&tv_start, NULL);                                \
		(function)();                                                 \
		gettimeofday(&tv_stop, NULL);                                 \
		fprintf(stdout, "ok (%ld ms)\n",                              \
			(tv_stop.tv_sec - tv_start.tv_sec) * 1000 +           \
				(tv_stop.tv_usec - tv_start.tv_usec) / 1000); \
	} while (0);
#else // YB_SUPPORT_FOUND
/* YB: OdUnit runner, runs each test in a forked child */
extern void yb_od_unit_init(int argc, char *argv[]);
extern void yb_od_unit_suite(const char *suite);
extern void yb_od_unit_run(const char *function, void (*test_function)(void));
extern int yb_od_unit_finish(void);
#define odyssey_test(function) yb_od_unit_run(#function, function);
#endif // YB_SUPPORT_FOUND

#define test(expression)                                                    \
	do {                                                                \
		if (!(expression)) {                                        \
			fprintf(stdout,                                     \
				"fail (%s:%d) with error \"%s\" (%d) %s\n", \
				__FILE__, __LINE__, strerror(errno), errno, \
				#expression);                               \
			fflush(stdout);                                     \
			abort();                                            \
		}                                                           \
	} while (0);

#endif /* ODYSSEY_TEST_H */
