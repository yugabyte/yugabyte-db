/*
 * YB: OdRegress runner. Runs each odyssey_test() in a forked child and
 * understands the gtest flags that the YugabyteDB test infrastructure uses.
 */

#include <errno.h>
#include <fnmatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <odyssey_test.h>

#define YB_OD_REGRESS_MAX_TESTS 1024
#define YB_OD_REGRESS_NAME_LEN 256
#define YB_OD_REGRESS_TIMEOUT_MS (300 * 1000)

typedef struct {
	char suite[YB_OD_REGRESS_NAME_LEN];
	char name[YB_OD_REGRESS_NAME_LEN];
	char failure[YB_OD_REGRESS_NAME_LEN];
	long time_ms;
	int failed;
} yb_od_regress_result_t;

static yb_od_regress_result_t yb_results[YB_OD_REGRESS_MAX_TESTS];
static int yb_results_count = 0;
static int yb_list_only = 0;
static const char *yb_filter = "*";
static const char *yb_xml_path = NULL;
static const char *yb_suite = "OdRegress";
static const char *yb_listed_suite = NULL;

static long yb_now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int yb_match_any(const char *patterns, size_t len, const char *full_name)
{
	char pattern[YB_OD_REGRESS_NAME_LEN];
	const char *pos = patterns;
	const char *end = patterns + len;

	while (pos <= end) {
		const char *sep = memchr(pos, ':', end - pos);
		size_t pattern_len = (sep ? sep : end) - pos;

		if (pattern_len < sizeof(pattern)) {
			memcpy(pattern, pos, pattern_len);
			pattern[pattern_len] = '\0';
			if (fnmatch(pattern, full_name, 0) == 0)
				return 1;
		}
		if (sep == NULL)
			break;
		pos = sep + 1;
	}
	return 0;
}

static int yb_filter_matches(const char *full_name)
{
	const char *negative = strchr(yb_filter, '-');
	size_t positive_len = negative ? (size_t)(negative - yb_filter) :
					 strlen(yb_filter);

	if (positive_len == 0) {
		if (negative == NULL)
			return 0;
	} else if (!yb_match_any(yb_filter, positive_len, full_name)) {
		return 0;
	}
	if (negative != NULL &&
	    yb_match_any(negative + 1, strlen(negative + 1), full_name))
		return 0;
	return 1;
}

static const char *yb_short_name(const char *function)
{
	static const char *prefixes[] = { "machinarium_test_", "odyssey_test_" };
	size_t i;

	for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		size_t len = strlen(prefixes[i]);
		if (strncmp(function, prefixes[i], len) == 0)
			return function + len;
	}
	return function;
}

void yb_od_regress_init(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--gtest_list_tests") == 0)
			yb_list_only = 1;
		else if (strncmp(argv[i], "--gtest_filter=", 15) == 0)
			yb_filter = argv[i] + 15;
		else if (strncmp(argv[i], "--gtest_output=xml:", 19) == 0)
			yb_xml_path = argv[i] + 19;
	}
}

void yb_od_regress_suite(const char *suite)
{
	yb_suite = suite;
}

static void yb_wait_for_child(pid_t pid, yb_od_regress_result_t *result)
{
	long deadline = yb_now_ms() + YB_OD_REGRESS_TIMEOUT_MS;
	int status;

	for (;;) {
		pid_t rc = waitpid(pid, &status, WNOHANG);

		if (rc == pid)
			break;
		if (rc == -1 && errno != EINTR) {
			result->failed = 1;
			snprintf(result->failure, sizeof(result->failure),
				 "waitpid failed: %s", strerror(errno));
			return;
		}
		if (yb_now_ms() >= deadline) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			result->failed = 1;
			snprintf(result->failure, sizeof(result->failure),
				 "timed out after %d ms",
				 YB_OD_REGRESS_TIMEOUT_MS);
			return;
		}
		usleep(1000);
	}

	if (WIFSIGNALED(status)) {
		result->failed = 1;
		snprintf(result->failure, sizeof(result->failure),
			 "terminated by signal %d (%s)", WTERMSIG(status),
			 strsignal(WTERMSIG(status)));
	} else if (WEXITSTATUS(status) != 0) {
		result->failed = 1;
		snprintf(result->failure, sizeof(result->failure),
			 "exited with status %d", WEXITSTATUS(status));
	}
}

void yb_od_regress_run(const char *function, void (*test_function)(void))
{
	char full_name[2 * YB_OD_REGRESS_NAME_LEN];
	const char *name = yb_short_name(function);
	yb_od_regress_result_t *result;
	long start_ms;
	pid_t pid;

	snprintf(full_name, sizeof(full_name), "%s.%s", yb_suite, name);
	if (!yb_filter_matches(full_name))
		return;

	if (yb_list_only) {
		if (yb_listed_suite == NULL ||
		    strcmp(yb_listed_suite, yb_suite) != 0) {
			printf("%s.\n", yb_suite);
			yb_listed_suite = yb_suite;
		}
		printf("  %s\n", name);
		return;
	}

	if (yb_results_count == YB_OD_REGRESS_MAX_TESTS) {
		printf("too many tests, increase YB_OD_REGRESS_MAX_TESTS\n");
		exit(1);
	}
	result = &yb_results[yb_results_count++];
	snprintf(result->suite, sizeof(result->suite), "%s", yb_suite);
	snprintf(result->name, sizeof(result->name), "%s", name);

	printf("[ RUN      ] %s\n", full_name);
	fflush(stdout);
	fflush(stderr);

	start_ms = yb_now_ms();
	pid = fork();
	if (pid == 0) {
		test_function();
		fflush(stdout);
		fflush(stderr);
		_exit(0);
	}
	if (pid == -1) {
		result->failed = 1;
		snprintf(result->failure, sizeof(result->failure),
			 "fork failed: %s", strerror(errno));
	} else {
		yb_wait_for_child(pid, result);
	}
	result->time_ms = yb_now_ms() - start_ms;

	if (result->failed)
		printf("%s: %s\n", full_name, result->failure);
	printf("[ %s ] %s (%ld ms)\n",
	       result->failed ? " FAILED " : "      OK", full_name,
	       result->time_ms);
	fflush(stdout);
}

static void yb_write_xml(int failed_count, long total_ms)
{
	FILE *xml;
	int i;

	xml = fopen(yb_xml_path, "w");
	if (xml == NULL) {
		printf("cannot write %s: %s\n", yb_xml_path, strerror(errno));
		return;
	}
	fprintf(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(xml,
		"<testsuites tests=\"%d\" failures=\"%d\" disabled=\"0\" "
		"errors=\"0\" time=\"%.3f\" name=\"AllTests\">\n",
		yb_results_count, failed_count, total_ms / 1000.0);
	for (i = 0; i < yb_results_count; i++) {
		yb_od_regress_result_t *result = &yb_results[i];

		fprintf(xml,
			"  <testsuite name=\"%s\" tests=\"1\" failures=\"%d\" "
			"disabled=\"0\" errors=\"0\" time=\"%.3f\">\n",
			result->suite, result->failed,
			result->time_ms / 1000.0);
		fprintf(xml,
			"    <testcase name=\"%s\" status=\"run\" "
			"time=\"%.3f\" classname=\"%s\"",
			result->name, result->time_ms / 1000.0, result->suite);
		if (result->failed)
			fprintf(xml,
				">\n      <failure message=\"%s\" type=\"\"/>\n"
				"    </testcase>\n",
				result->failure);
		else
			fprintf(xml, " />\n");
		fprintf(xml, "  </testsuite>\n");
	}
	fprintf(xml, "</testsuites>\n");
	fclose(xml);
}

int yb_od_regress_finish(void)
{
	int failed_count = 0;
	long total_ms = 0;
	int i;

	if (yb_list_only)
		return 0;

	for (i = 0; i < yb_results_count; i++) {
		failed_count += yb_results[i].failed;
		total_ms += yb_results[i].time_ms;
	}

	printf("[==========] %d tests ran. (%ld ms total)\n", yb_results_count,
	       total_ms);
	printf("[  PASSED  ] %d tests.\n", yb_results_count - failed_count);
	if (failed_count > 0) {
		printf("[  FAILED  ] %d tests, listed below:\n", failed_count);
		for (i = 0; i < yb_results_count; i++) {
			if (yb_results[i].failed)
				printf("[  FAILED  ] %s.%s\n",
				       yb_results[i].suite, yb_results[i].name);
		}
	}
	fflush(stdout);

	if (yb_xml_path != NULL)
		yb_write_xml(failed_count, total_ms);

	return failed_count > 0 ? 1 : 0;
}
