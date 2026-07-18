
#include <machinarium.h>
#include <odyssey_test.h>

#include <arpa/inet.h>

static void test_connect_coroutine(void *arg)
{
	(void)arg;
	machine_io_t *client = machine_io_create();
	test(client != NULL);

	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");
	sa.sin_port = htons(81);
	int rc;
	rc = machine_connect(client, (struct sockaddr *)&sa, UINT32_MAX);
	if (rc == -1) {
		int errno_ = machine_errno();
		test(errno_ == ECONNREFUSED || errno_ == ECONNRESET);
	} else {
		machine_close(client);
	}

	machine_io_free(client);
}

static void test_waiter(void *arg)
{
	(void)arg;
	int id = machine_coroutine_create(test_connect_coroutine, NULL);
	test(id != -1);

	machine_sleep(0);

	int rc;
	rc = machine_join(id);
	test(rc == 0);

	machine_stop_current();
}

void machinarium_test_connect(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_waiter, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
