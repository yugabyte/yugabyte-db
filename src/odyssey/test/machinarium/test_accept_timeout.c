
#include <machinarium.h>
#include <odyssey_test.h>

#include <arpa/inet.h>

static void test_server(void *arg)
{
	(void)arg;
	machine_io_t *server = machine_io_create();
	test(server != NULL);

	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");
	sa.sin_port = htons(7778);
	int rc;
	rc = machine_bind(server, (struct sockaddr *)&sa,
			  MM_BINDWITH_SO_REUSEADDR);
	test(rc == 0);

	machine_io_t *client;
	rc = machine_accept(server, &client, 16, 1, 100);
	test(rc == -1);
	test(machine_timedout());

	rc = machine_close(server);
	test(rc == 0);

	machine_io_free(server);
}

void machinarium_test_accept_timeout(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_server, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
