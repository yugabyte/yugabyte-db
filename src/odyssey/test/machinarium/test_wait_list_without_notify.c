#include <machinarium.h>
#include <odyssey_test.h>

static inline void test_wait_without_notify_coroutine(void *arg)
{
	(void)arg;

	machine_wait_list_t *wait_list = machine_wait_list_create();

	uint64_t start, end;
	start = machine_time_ms();

	int rc;
	rc = machine_wait_list_wait(wait_list, 1000);
	end = machine_time_ms();
	test(rc == 1);
	test(end - start >= 1000);

	// notify without waiters should be ignored
	machine_wait_list_notify(wait_list);
	rc = machine_wait_list_wait(wait_list, 1000);
	test(rc == 1);

	machine_wait_list_destroy(wait_list);
}

static inline void test_wait_without_notify(void *arg)
{
	(void)arg;

	int id;
	id = machine_coroutine_create(test_wait_without_notify_coroutine, NULL);
	test(id != -1);

	machine_sleep(0);

	int rc;
	rc = machine_join(id);
	test(rc == 0);
}

void machinarium_test_wait_list_without_notify()
{
	machinarium_init();

	int id;
	id = machine_create("test_wait_list_wait_without_notify",
			    test_wait_without_notify, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
