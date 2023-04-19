
#include <machinarium.h>
#include <odyssey_test.h>

static void test_sleep_cancel0_child(void *arg)
{
	(void)arg;
	machine_sleep(6000000);
	test(machine_cancelled())
}

static void test_sleep_cancel0_parent(void *arg)
{
	(void)arg;
	int64_t id;
	id = machine_coroutine_create(test_sleep_cancel0_child, NULL);
	test(id != -1);

	mm_yield;

	int rc;
	rc = machine_cancel(id);
	test(rc == 0);

	rc = machine_join(id);
	test(rc == 0);

	machine_stop_current();
}

void machinarium_test_sleep_cancel0(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_sleep_cancel0_parent, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
