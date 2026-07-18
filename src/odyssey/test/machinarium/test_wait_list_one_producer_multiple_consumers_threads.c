#include <machinarium.h>
#include <odyssey_test.h>

static inline void producer_coroutine(void *arg)
{
	machine_wait_list_t *wl = arg;

	uint64_t start, current;
	start = machine_time_ms();
	current = start;

	while ((current - start) < 4000) {
		machine_wait_list_notify(wl);
		machine_sleep(300);
		current = machine_time_ms();
	}
}

typedef struct {
	machine_wait_list_t *wl;
	int count;
	int64_t id;
} consumer_arg_t;

static inline void consumer_coroutine(void *arg)
{
	consumer_arg_t *ca = arg;
	machine_wait_list_t *wl = ca->wl;
	int *count = &ca->count;

	uint64_t start, current;
	int rc;
	start = machine_time_ms();
	current = start;

	while ((current - start) < 3000) {
		rc = machine_wait_list_wait(wl, 1000);
		test(rc == 0);
		++(*count);
		current = machine_time_ms();
	}
}

static inline void test_multiple_consumers(void *arg)
{
	(void)arg;

	machine_wait_list_t *wl = machine_wait_list_create();

	int producer_id;
	producer_id = machine_coroutine_create(producer_coroutine, wl);
	test(producer_id != -1);

	consumer_arg_t a1, a2, a3;
	int c1, c2, c3;

	a1.count = a2.count = a3.count = 0;
	a1.wl = a2.wl = a3.wl = wl;

	c1 = machine_coroutine_create(consumer_coroutine, &a1);
	test(c1 != -1);
	a1.id = c1;

	c2 = machine_coroutine_create(consumer_coroutine, &a2);
	test(c2 != -1);
	a2.id = c2;

	c3 = machine_coroutine_create(consumer_coroutine, &a3);
	test(c3 != -1);
	a3.id = c3;

	machine_sleep(0);

	int rc;
	rc = machine_join(producer_id);
	test(rc == 0);

	test(a1.count >= 3);
	test(a2.count >= 3);
	test(a3.count >= 3);

	machine_wait_list_destroy(wl);
}

void machinarium_test_wait_list_one_producer_multiple_consumers_threads()
{
	machinarium_init();

	int id;
	id = machine_create(
		"test_wait_list_one_producer_multiple_consumers_threads",
		test_multiple_consumers, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
