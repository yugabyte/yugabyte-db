
#include <machinarium.h>
#include <odyssey_test.h>

static void coroutine(void *arg)
{
	(void)arg;
	machine_sleep(0);
	machine_stop_current();
}

void machinarium_test_sleep_yield(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", coroutine, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
