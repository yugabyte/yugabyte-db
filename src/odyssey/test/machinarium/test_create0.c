
#include <machinarium.h>
#include <odyssey_test.h>

static int coroutine_call = 0;

static void coroutine(void *arg)
{
	(void)arg;
	coroutine_call++;
	machine_stop_current();
}

void machinarium_test_create0(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", coroutine, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	test(coroutine_call == 1);

	machinarium_free();
}
