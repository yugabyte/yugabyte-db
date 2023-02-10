
#include <machinarium.h>
#include <odyssey_test.h>

static machine_channel_t *channel;

static int count_written = 0;
static int count_read = 0;
static int pc;

static void test_consumer(void *arg)
{
	(void)arg;

	while (count_read < count_written) {
		machine_msg_t *msg;
		msg = machine_channel_read(channel, 0);
		if (msg == NULL)
			break;
		machine_msg_free(msg);
		count_read++;
		machine_sleep(0);
	}
}

static void test_pc(void *arg)
{
	(void)arg;

	int i = 0;
	for (; i < 1000; i++) {
		machine_msg_t *msg;
		msg = machine_msg_create(0);
		test(msg != NULL);
		machine_msg_set_type(msg, i);
		count_written++;
		machine_channel_write(channel, msg);
	}

	for (i = 0; i < 100; i++) {
		int rc = machine_coroutine_create(test_consumer, NULL);
		test(rc != -1);
	}
}

void machinarium_test_producer_consumer2(void)
{
	machinarium_init();

	channel = machine_channel_create(0);
	test(channel != NULL);

	pc = machine_create("producer-consumer", test_pc, NULL);
	test(pc != -1);

	int rc;
	rc = machine_wait(pc);
	test(rc != -1);

	test(count_read == 1000);
	test(count_written == count_read);

	machine_channel_free(channel);

	machinarium_free();
}
