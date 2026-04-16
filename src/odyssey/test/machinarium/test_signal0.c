
#include <machinarium.h>
#include <odyssey_test.h>

#include <unistd.h>
#include <signal.h>

static void coroutine(void *arg)
{
	(void)arg;

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	sigset_t ignore;
	sigemptyset(&ignore);

	int rc;
	rc = machine_signal_init(&mask, &ignore);
	test(rc == 0);

	rc = kill(getpid(), SIGINT);
	test(rc == 0);

	rc = machine_signal_wait(UINT32_MAX);
	test(rc == SIGINT);
}

void machinarium_test_signal0(void)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	machinarium_init();

	int id;
	id = machine_create("test", coroutine, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();

	sigprocmask(SIG_UNBLOCK, &mask, NULL);
}
