
#include <machinarium.h>
#include <odyssey_test.h>

static machine_channel_t *channel;

static void test_coroutine_a(void *arg)
{
	(void)arg;
	machine_msg_t *msg;
	msg = machine_channel_read(channel, UINT32_MAX);
	test(msg != NULL);
	test(machine_msg_type(msg) == 1);
	machine_msg_free(msg);
}

static void test_coroutine_b(void *arg)
{
	(void)arg;
	machine_msg_t *msg;
	msg = machine_channel_read(channel, UINT32_MAX);
	test(msg != NULL);
	test(machine_msg_type(msg) == 2);
	machine_msg_free(msg);
}

static void test_coroutine_c(void *arg)
{
	(void)arg;
	machine_msg_t *msg;
	msg = machine_channel_read(channel, UINT32_MAX);
	test(msg != NULL);
	test(machine_msg_type(msg) == 3);
	machine_msg_free(msg);
}

static void test_coroutine(void *arg)
{
	(void)arg;
	channel = machine_channel_create(0);
	test(channel != NULL);

	int a;
	a = machine_coroutine_create(test_coroutine_a, NULL);
	test(a != -1);
	machine_sleep(0);

	int b;
	b = machine_coroutine_create(test_coroutine_b, NULL);
	test(b != -1);
	machine_sleep(0);

	int c;
	c = machine_coroutine_create(test_coroutine_c, NULL);
	test(c != -1);
	machine_sleep(0);

	machine_msg_t *msg;
	msg = machine_msg_create(0);
	test(msg != NULL);
	machine_msg_set_type(msg, 1);
	machine_channel_write(channel, msg);
	machine_sleep(0);

	msg = machine_msg_create(0);
	test(msg != NULL);
	machine_msg_set_type(msg, 2);
	machine_channel_write(channel, msg);
	machine_sleep(0);

	msg = machine_msg_create(0);
	test(msg != NULL);
	machine_msg_set_type(msg, 3);
	machine_channel_write(channel, msg);
	machine_sleep(0);

	machine_join(a);
	machine_join(b);
	machine_join(c);

	machine_channel_free(channel);
}

void machinarium_test_channel_rw4(void)
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
