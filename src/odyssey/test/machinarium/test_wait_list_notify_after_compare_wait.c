#include <machinarium.h>
#include <odyssey_test.h>
#include <stdatomic.h>

typedef struct {
	machine_wait_list_t *wl;
	atomic_uint_fast64_t *wl_word;
} test_arg_t;

static inline void producer(void *arg)
{
	test_arg_t *t_arg = arg;
	machine_wait_list_t *wl = t_arg->wl;
	atomic_uint_fast64_t *wl_word = t_arg->wl_word;

	machine_sleep(500);
	atomic_store(wl_word, 1);
	machine_wait_list_notify(wl);
}

static inline void consumer(void *arg)
{
	test_arg_t *t_arg = arg;
	machine_wait_list_t *wl = t_arg->wl;
	atomic_uint_fast64_t *wl_word = t_arg->wl_word;

	uint64_t start, end, total_time;
	int rc;

	start = machine_time_ms();
	rc = machine_wait_list_compare_wait(wl, 0, 1000);
	end = machine_time_ms();
	test(rc == 0);
	total_time = end - start;
	test(total_time > 400 && total_time < 1000);
	test(atomic_load(wl_word) == 1)
}

static inline void test_notify_after_compare_wait_coroutines(void *arg)
{
	(void)arg;

	atomic_uint_fast64_t atomic;
	atomic_store(&atomic, 0);

	machine_wait_list_t *wl = machine_wait_list_create(&atomic);

	test_arg_t t_arg;
	t_arg.wl = wl;
	t_arg.wl_word = &atomic;

	int consumer_id;
	consumer_id = machine_coroutine_create(consumer, &t_arg);
	test(consumer_id != -1);

	int producer_id;
	producer_id = machine_coroutine_create(producer, &t_arg);
	test(producer_id != -1);

	int rc;
	rc = machine_join(producer_id);
	test(rc == 0);

	rc = machine_join(consumer_id);
	test(rc == 0);

	machine_wait_list_destroy(wl);
}

static inline void test_notify_after_compare_wait_threads(void *arg)
{
	(void)arg;

	atomic_uint_fast64_t atomic;
	atomic_store(&atomic, 0);

	machine_wait_list_t *wl = machine_wait_list_create(&atomic);

	test_arg_t t_arg;
	t_arg.wl = wl;
	t_arg.wl_word = &atomic;

	int consumer_id;
	consumer_id = machine_create("consumer_thread", consumer, &t_arg);
	test(consumer_id != -1);

	int producer_id;
	producer_id = machine_create("producer_thread", producer, &t_arg);
	test(producer_id != -1);

	int rc;
	rc = machine_wait(producer_id);
	test(rc == 0);

	rc = machine_wait(consumer_id);
	test(rc == 0);

	machine_wait_list_destroy(wl);
}

void machinarium_test_wait_list_notify_after_compare_wait()
{
	machinarium_init();

	int id;
	id = machine_create(
		"test_wait_list_notify_after_compare_wait_coroutines",
		test_notify_after_compare_wait_coroutines, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	id = machine_create("test_wait_list_notify_after_compare_wait_threads",
			    test_notify_after_compare_wait_threads, NULL);
	test(id != -1);

	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}