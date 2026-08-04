
/*
 * machinarium.
 *
 * Cooperative multitasking engine.
 */

/*
 * This example shows coroutine context switch (yield)
 * performance done in one second.
 */

#include <machinarium.h>

static int csw = 0;

static void benchmark_worker(void *arg)
{
	printf("worker started.\n");
	while (machine_active()) {
		csw++;
		machine_sleep(0);
	}
	printf("worker done.\n");
}

static void benchmark_runner(void *arg)
{
	printf("benchmark started.\n");
	machine_coroutine_create(benchmark_worker, NULL);
	machine_sleep(1000);
	printf("done.\n");
	printf("context switches %d in 1 sec.\n", csw);
	machine_stop_current();
}

int main(int argc, char *argv[])
{
	machinarium_init();
	int id = machine_create("benchmark_csw", benchmark_runner, NULL);
	int rc = machine_wait(id);
	printf("retcode from machine wait %d.\n", rc);
	machinarium_free();
	return 0;
}
