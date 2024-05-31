
#include <machinarium.h>
#include <odyssey_test.h>

static machine_channel_t *channel;
static int consumers_count = 5;
static int consumers_stat[5] = { 0 };

static void test_consumer(void *arg)
{
	uintptr_t consumer_id = (uintptr_t)arg;
	for (;;) {
		machine_msg_t *msg;
		msg = machine_channel_read(channel, UINT32_MAX);
		test(msg != NULL);
		consumers_stat[consumer_id]++;
		int is_exit = (uint32_t)machine_msg_type(msg) == UINT32_MAX;
		machine_msg_free(msg);
		if (is_exit)
			break;
	}
}

static void test_producer(void *arg)
{
	(void)arg;
	int i = 0;
	for (; i < 100000; i++) {
		machine_msg_t *msg;
		msg = machine_msg_create(0);
		test(msg != NULL);
		machine_msg_set_type(msg, i);
		machine_channel_write(channel, msg);
	}
	/* exit */
	for (i = 0; i < consumers_count; i++) {
		machine_msg_t *msg;
		msg = machine_msg_create(0);
		test(msg != NULL);
		machine_msg_set_type(msg, UINT32_MAX);
		machine_channel_write(channel, msg);
	}
}

void machinarium_test_producer_consumer1(void)
{
	machinarium_init();

	channel = machine_channel_create(1);
	test(channel != NULL);

	int producer;
	producer = machine_create("producer", test_producer, NULL);
	test(producer != -1);

	int consumers[consumers_count];
	uintptr_t i = 0;
	for (; (int)i < consumers_count; i++) {
		consumers[i] =
			machine_create("consumer", test_consumer, (void *)i);
		test(consumers[i] != -1);
	}

	int rc;
	rc = machine_wait(producer);
	test(rc != -1);

	printf("[");
	for (i = 0; (int)i < consumers_count; i++) {
		rc = machine_wait(consumers[i]);
		test(rc != -1);
		if (i > 0)
			printf(", ");
		printf("%d", consumers_stat[i]);
		fflush(NULL);
	}
	printf("] ");

	machine_channel_free(channel);

	machinarium_free();
}
