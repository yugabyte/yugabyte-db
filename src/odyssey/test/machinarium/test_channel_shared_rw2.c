
#include <machinarium.h>
#include <odyssey_test.h>

static machine_channel_t *channel;

static void test_coroutine2(void *arg)
{
	(void)arg;
	machine_msg_t *msg;
	msg = machine_channel_read(channel, UINT32_MAX);
	test(msg != NULL);
	test(machine_msg_type(msg) == 123);
	machine_msg_free(msg);

	msg = machine_msg_create(0);
	machine_msg_set_type(msg, 321);
	machine_channel_write(channel, msg);
}

static void test_coroutine(void *arg)
{
	(void)arg;
	channel = machine_channel_create(1);
	test(channel != NULL);

	int id;
	id = machine_coroutine_create(test_coroutine2, NULL);
	machine_sleep(0);

	machine_msg_t *msg;
	msg = machine_msg_create(0);
	test(msg != NULL);
	machine_msg_set_type(msg, 123);
	machine_channel_write(channel, msg);

	machine_sleep(0);
	machine_sleep(0);

	msg = machine_channel_read(channel, UINT32_MAX);
	test(msg != NULL);
	test(machine_msg_type(msg) == 321);
	machine_msg_free(msg);

	machine_join(id);

	machine_channel_free(channel);
}

void machinarium_test_channel_shared_rw2(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_coroutine, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
