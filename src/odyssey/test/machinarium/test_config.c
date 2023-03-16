
#include <machinarium.h>
#include <odyssey_test.h>
#include <unistd.h>

void machinarium_test_config(void)
{
	machinarium_set_pool_size(1);
	machinarium_init();
	test(machinarium_init() == -1);
	machinarium_free();
}
