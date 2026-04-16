
#include <machinarium.h>
#include <odyssey_test.h>
#include <unistd.h>

#if 0
static void
coroutine(void *arg)
{
	(void)arg;
	machine_sleep(100);
}
#endif

void machinarium_test_stat(void)
{
	machinarium_init();

#if 0
	uint64_t count_machine = 0;
	uint64_t count_coroutine = 0;
	uint64_t count_coroutine_cache = 0;
	uint64_t msg_allocated = 0;
	uint64_t msg_cache_count = 0;
	uint64_t msg_cache_gc_count = 0;
	uint64_t msg_cache_size = 0;


	for (;;) {
		machinarium_stat(&count_machine, &count_coroutine,
		                 &count_coroutine_cache,
		                 &msg_allocated,
		                 &msg_cache_count,
		                 &msg_cache_gc_count,
		                 &msg_cache_size);
		test(count_machine == 3); /* thread pool */
		test(count_coroutine_cache == 0);
		if (count_coroutine != 3) {
			usleep(10000);
			continue;
		}
		break;
	}

	int id;
	id = machine_create("test", coroutine, NULL);
	test(id != -1);

	for (;;) {
		machinarium_stat(&count_machine, &count_coroutine,
		                 &count_coroutine_cache,
		                 &msg_allocated,
		                 &msg_cache_count,
		                 &msg_cache_gc_count,
		                 &msg_cache_size);
		test(count_machine == 3 + 1);
		test(count_coroutine_cache == 0);
		if (count_coroutine != 4) {
			usleep(10000);
			continue;
		}
		break;
	}

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	for (;;) {
		machinarium_stat(&count_machine, &count_coroutine,
		                 &count_coroutine_cache,
		                 &msg_allocated,
		                 &msg_cache_count,
		                 &msg_cache_gc_count,
		                 &msg_cache_size);
		test(count_machine == 3)
		if (count_coroutine != 3) {
			usleep(10000);
			continue;
		}
		test(count_coroutine_cache == 0);
		break;
	}
#endif

	machinarium_free();
}
