
#include <machinarium.h>
#include <odyssey_test.h>

static machine_cond_t *condition = NULL;

static void test_condition_coroutine(void *arg)
{
	(void)arg;
	machine_cond_signal(condition);
	machine_stop_current();
}

static void test_waiter(void *arg)
{
	(void)arg;

	condition = machine_cond_create();
	test(condition != NULL);

	int64_t a;
	a = machine_coroutine_create(test_condition_coroutine, NULL);
	test(a != -1);

	int rc;
	rc = machine_cond_wait(condition, UINT32_MAX);
	test(rc == 0);

	machine_stop_current();
}

void machinarium_test_condition0(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_waiter, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
