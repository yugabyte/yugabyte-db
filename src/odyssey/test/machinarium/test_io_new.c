
#include <machinarium.h>
#include <odyssey_test.h>

static void coroutine(void *arg)
{
	(void)arg;
	machine_io_t *io = machine_io_create();
	test(io != NULL);

	machine_io_free(io);
}

void machinarium_test_io_new(void)
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
