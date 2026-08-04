
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

#define MAX_COROUTINES 256

int csws[MAX_COROUTINES];

static void benchmark_worker(void *arg)
{
	int id = arg;
	//	printf("worker started.\n");
	while (machine_active()) {
		csws[id]++;
		machine_sleep(0);
	}
	//	printf("worker done.\n");
}

static void benchmark_runner(void *arg)
{
	printf("benchmark started.\n");
	for (int i = 0; i < MAX_COROUTINES; ++i) {
		machine_coroutine_create(benchmark_worker, i);
	}
	machine_sleep(1000);
	printf("done.\n");

	int csw = 0;
	for (int i = 0; i < MAX_COROUTINES; ++i) {
		csw += csws[i];
	}
	fflush(stdout);
	printf("_______________________________\n");
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
