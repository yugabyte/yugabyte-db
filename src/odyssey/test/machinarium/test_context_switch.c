
#include <machinarium.h>
#include <odyssey_test.h>

static int csw = 0;

static void csw_worker(void *arg)
{
	(void)arg;
	while (csw < 100000) {
		machine_sleep(0);
		csw++;
	}
}

static void csw_runner(void *arg)
{
	(void)arg;
	int rc;
	rc = machine_coroutine_create(csw_worker, NULL);
	test(rc != -1);

	rc = machine_join(rc);
	test(rc != -1);
	test(csw == 100000);

	machine_stop_current();
}

void machinarium_test_context_switch(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", csw_runner, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
