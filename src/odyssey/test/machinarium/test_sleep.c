
#include <machinarium.h>
#include <odyssey_test.h>

static void coroutine(void *arg)
{
	(void)arg;
	machine_sleep(100);
	machine_stop_current();
}

void machinarium_test_sleep(void)
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

static void coroutine_random_sleep(void *arg)
{
	(void)arg;
	long int duration = machine_lrand48();
	machine_sleep(duration & 0xFF);
	machine_stop_current();
}

void machinarium_test_sleep_random(void)
{
	machinarium_init();

	int n = 100;
	int id[n];
	for (int i = 0; i < n; i++) {
		id[i] = machine_create("test", coroutine_random_sleep, NULL);
		test(id[i] != -1);
	}

	for (int i = 0; i < n; i++) {
		int rc;
		rc = machine_wait(id[i]);
		test(rc != -1);
	}

	machinarium_free();
}
