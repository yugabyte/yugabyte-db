
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
#define ALLOC_SZ 2048

ssize_t corotine_alloced[MAX_COROUTINES];

static void benchmark_worker(void *arg)
{
	ssize_t i = arg;
	machine_msg_t *msg;
	//	printf("worker started.\n");
	while (machine_active()) {
		msg = machine_msg_create(ALLOC_SZ);
		corotine_alloced[i] += ALLOC_SZ;
		machine_sleep(0);
		machine_msg_free(msg);
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

	ssize_t tot = 0;
	for (int i = 0; i <= 1 && i < MAX_COROUTINES; ++i) {
		tot += corotine_alloced[i];
	}

	fflush(stdout);
	printf("_______________________________\n");
	printf("memory alloc %d in 1 sec.\n", tot);
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
