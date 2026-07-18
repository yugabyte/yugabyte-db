#include <machinarium.h>
#include <odyssey_test.h>

static inline void producer_coroutine(void *arg)
{
	machine_wait_list_t *wl = arg;

	machine_sleep(500);
	machine_wait_list_notify(wl);
}

static inline void consumer_coroutine(void *arg)
{
	machine_wait_list_t *wl = arg;

	uint64_t start, end, total_time;
	int rc;

	start = machine_time_ms();
	rc = machine_wait_list_wait(wl, 1000);
	end = machine_time_ms();
	test(rc == 0);
	total_time = end - start;
	test(total_time > 400 && total_time < 1000);
}

static inline void test_notify_after_wait(void *arg)
{
	(void)arg;

	machine_wait_list_t *wl = machine_wait_list_create();

	int consumer_id;
	consumer_id = machine_coroutine_create(consumer_coroutine, wl);
	test(consumer_id != -1);

	int producer_id;
	producer_id = machine_coroutine_create(producer_coroutine, wl);
	test(producer_id != -1);

	machine_sleep(0);

	int rc;
	rc = machine_join(producer_id);
	test(rc == 0);

	rc = machine_join(consumer_id);
	test(rc == 0);

	machine_wait_list_destroy(wl);
}

void machinarium_test_wait_list_notify_after_wait()
{
	machinarium_init();

	int id;
	id = machine_create("test_wait_list_notify_after_wait",
			    test_notify_after_wait, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
