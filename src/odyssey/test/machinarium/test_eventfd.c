
#include <machinarium.h>
#include <odyssey_test.h>

static void test_condition_coroutine(void *arg)
{
	machine_io_t *io = arg;
	uint64_t wakeup_ = 123;
	int rc;
	/* ignore the processed because we expect positive RC */
	size_t processed = 0;
	rc = machine_write_raw(io, (void *)&wakeup_, sizeof(wakeup_),
			       &processed);
	test(rc == sizeof(wakeup_));
}

static void test_waiter(void *arg)
{
	(void)arg;
	machine_io_t *event = machine_io_create();
	test(event != NULL);
	int rc;
	rc = machine_eventfd(event);
	test(rc == 0);

	rc = machine_io_attach(event);
	test(rc == 0);

	machine_cond_t *cond = machine_cond_create();
	test(cond != NULL);

	rc = machine_read_start(event, cond);
	test(rc == 0);

	int64_t a;
	a = machine_coroutine_create(test_condition_coroutine, event);
	test(a != -1);

	machine_cond_wait(cond, UINT32_MAX);

	uint64_t wakeup_;
	rc = machine_read_raw(event, &wakeup_, sizeof(uint64_t));
	test(rc == sizeof(wakeup_));

	machine_close(event);
	machine_io_free(event);

	machine_cond_free(cond);

	machine_stop_current();
}

void machinarium_test_eventfd0(void)
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
