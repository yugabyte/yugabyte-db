
#include <machinarium.h>
#include <odyssey_test.h>

static machine_channel_t *channel;

static int producer;
static int consumer;

static void test_consumer(void *arg)
{
	(void)arg;
	int i = 0;
	for (; i < 100; i++) {
		machine_msg_t *msg;
		msg = machine_channel_read(channel, UINT32_MAX);
		test(msg != NULL);
		machine_msg_free(msg);
	}
}

static void test_producer(void *arg)
{
	(void)arg;
	int i = 0;
	for (; i < 100; i++) {
		machine_msg_t *msg;
		msg = machine_msg_create(0);
		test(msg != NULL);
		machine_msg_set_type(msg, i);
		machine_channel_write(channel, msg);
	}
}

void machinarium_test_producer_consumer0(void)
{
	machinarium_init();

	channel = machine_channel_create(1);
	test(channel != NULL);

	producer = machine_create("producer", test_producer, NULL);
	test(producer != -1);

	consumer = machine_create("consumer", test_consumer, NULL);
	test(consumer != -1);

	int rc;
	rc = machine_wait(consumer);
	test(rc != -1);

	rc = machine_wait(producer);
	test(rc != -1);

	machine_channel_free(channel);

	machinarium_free();
}
