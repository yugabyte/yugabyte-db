
#include <machinarium.h>
#include <odyssey_test.h>

#include <string.h>
#include <arpa/inet.h>

static void server(void *arg)
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
	rc = machine_accept(server, &client, 16, 1, UINT32_MAX);
	test(rc == 0);
	machine_set_nodelay(client, 1);

	int chunk_size = 100 * 1024;
	int chunk_pos = 90 * 1024;
	while (chunk_pos < chunk_size) {
		machine_msg_t *msg;
		msg = machine_msg_create(0);
		test(msg != NULL);
		rc = machine_msg_write(msg, NULL, chunk_pos);
		test(rc == 0);
		memset(machine_msg_data(msg), 'x', chunk_pos);
		rc = machine_write(client, msg, UINT32_MAX);
		test(rc == 0);

		/* ack */
		msg = machine_read(client, sizeof(uint32_t), UINT32_MAX);
		test(msg != NULL);
		machine_msg_free(msg);

		chunk_pos++;
	}

	rc = machine_close(client);
	test(rc == 0);
	machine_io_free(client);

	rc = machine_close(server);
	test(rc == 0);
	machine_io_free(server);
}

static void client(void *arg)
{
	(void)arg;
	machine_io_t *client = machine_io_create();
	test(client != NULL);
	machine_set_nodelay(client, 1);

	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");
	sa.sin_port = htons(7778);
	int rc;
	rc = machine_connect(client, (struct sockaddr *)&sa, UINT32_MAX);
	test(rc == 0);

	int chunk_size = 100 * 1024;

	char *chunk_cmp = malloc(chunk_size);
	test(chunk_cmp != NULL);
	memset(chunk_cmp, 'x', chunk_size);

	int chunk_pos = 90 * 1024;
	while (chunk_pos < chunk_size) {
		machine_msg_t *msg;
		msg = machine_read(client, chunk_pos, UINT32_MAX);
		test(msg != NULL);
		test(memcmp(machine_msg_data(msg), chunk_cmp, chunk_pos) == 0);
		machine_msg_free(msg);

		msg = machine_msg_create(0);
		uint32_t ack = 1;
		rc = machine_msg_write(msg, (void *)&ack, sizeof(ack));
		test(rc == 0);
		rc = machine_write(client, msg, UINT32_MAX);
		test(rc == 0);

		chunk_pos++;
	}

	free(chunk_cmp);

	rc = machine_close(client);
	test(rc == 0);
	machine_io_free(client);
}

static void test_cs(void *arg)
{
	(void)arg;
	int rc;
	rc = machine_coroutine_create(server, NULL);
	test(rc != -1);

	rc = machine_coroutine_create(client, NULL);
	test(rc != -1);
}

void machinarium_test_read_var(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_cs, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
