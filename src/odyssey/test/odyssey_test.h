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

/* YB: OdRegress runner, runs each test in a forked child */
extern void yb_od_regress_init(int argc, char *argv[]);
extern void yb_od_regress_suite(const char *suite);
extern void yb_od_regress_run(const char *function,
			      void (*test_function)(void));
extern int yb_od_regress_finish(void);
#define odyssey_test(function) yb_od_regress_run(#function, function);

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
