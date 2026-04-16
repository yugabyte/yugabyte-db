
#include <machinarium.h>
#include <odyssey_test.h>

static void test_coroutine(void *arg)
{
	(void)arg;
	machine_channel_t *channel;
	channel = machine_channel_create(0);
	test(channel != NULL);

	machine_msg_t *msg;
	msg = machine_msg_create(0);
	test(msg != NULL);
	machine_msg_set_type(msg, 123);

	machine_channel_write(channel, msg);

	machine_msg_t *msg_in;
	msg_in = machine_channel_read(channel, 0);
	test(msg_in != NULL);
	test(msg_in == msg);
	machine_msg_free(msg);

	machine_channel_free(channel);
}

void machinarium_test_channel_rw0(void)
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
