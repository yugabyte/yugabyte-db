
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
	sa.sin_addr.s_addr = inet_addr("8.8.8.8");
	sa.sin_port = htons(1234);
	int rc;
	rc = machine_connect(client, (struct sockaddr *)&sa, UINT32_MAX);
	test(rc == -1);
	test(machine_cancelled());

	machine_io_free(client);
}

static void test_waiter(void *arg)
{
	(void)arg;
	int id = machine_coroutine_create(test_connect_coroutine, NULL);
	test(id != -1);

	int rc;
	rc = machine_cancel(id);
	test(rc == 0);

	machine_sleep(0);

	rc = machine_join(id);
	test(rc == -1);

	machine_stop_current();
}

void machinarium_test_connect_cancel1(void)
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
