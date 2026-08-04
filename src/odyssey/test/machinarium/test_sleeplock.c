
#include <unistd.h>
#include <machinarium.h>
#include <sleep_lock.h>
#include <odyssey_test.h>

mm_sleeplock_t global_lock;

static void test_coroutine(void *arg)
{
	uint64_t *value = (uint64_t *)arg;
	for (int i = 0; i < (1 << 22); i++) {
		mm_sleeplock_lock(&global_lock);
		(*value)++;
		mm_sleeplock_unlock(&global_lock);
	}
}

void machinarium_test_sleeplock(void)
{
	machinarium_init();

	uint64_t value = 0;
	mm_sleeplock_init(&global_lock);

	int id0, id1, id2, id3;
	id0 = machine_create("test", test_coroutine, &value);
	id1 = machine_create("test", test_coroutine, &value);
	id2 = machine_create("test", test_coroutine, &value);
	id3 = machine_create("test", test_coroutine, &value);
	test(id0 != -1);
	test(id1 != -1);
	test(id2 != -1);
	test(id3 != -1);

	test(machine_wait(id0) != -1);
	test(machine_wait(id1) != -1);
	test(machine_wait(id2) != -1);
	test(machine_wait(id3) != -1);

	test(value == (4L << 22));

	machinarium_free();
}
