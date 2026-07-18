
#include <machinarium.h>
#include <odyssey_test.h>

#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>

static void server(void *arg)
{
	(void)arg;
	machine_io_t *server = machine_io_create();
	test(server != NULL);

	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, "_un_test", sizeof(sa.sun_path) - 1);
	int rc;
	rc = machine_bind(server, (struct sockaddr *)&sa,
			  MM_BINDWITH_SO_REUSEADDR);
	test(rc == 0);

	machine_io_t *client = NULL;
	rc = machine_accept(server, &client, 16, 1, UINT32_MAX);
	test(rc == 0);
	test(client != NULL);

	machine_tls_t *tls;
	tls = machine_tls_create();
	rc = machine_tls_set_verify(tls, "none");
	test(rc == 0);
	rc = machine_tls_set_ca_file(tls, "./machinarium/ca.crt");
	test(rc == 0);
	rc = machine_tls_set_cert_file(tls, "./machinarium/server.crt");
	test(rc == 0);
	rc = machine_tls_set_key_file(tls, "./machinarium/server.key");
	test(rc == 0);
	rc = machine_set_tls(client, tls, UINT32_MAX);
	if (rc == -1) {
		printf("%s\n", machine_error(client));
		test(rc == 0);
	}

	machine_msg_t *msg;
	msg = machine_msg_create(0);
	test(msg != NULL);
	char text[] = "hello world";
	rc = machine_msg_write(msg, text, sizeof(text));
	test(rc == 0);

	rc = machine_write(client, msg, UINT32_MAX);
	test(rc == 0);

	rc = machine_close(client);
	test(rc == 0);
	machine_io_free(client);

	rc = machine_close(server);
	test(rc == 0);
	machine_io_free(server);

	machine_tls_free(tls);
	unlink("_un_test");
}

static void client(void *arg)
{
	(void)arg;
	machine_io_t *client = machine_io_create();
	test(client != NULL);

	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, "_un_test", sizeof(sa.sun_path) - 1);
	int rc;
	rc = machine_connect(client, (struct sockaddr *)&sa, UINT32_MAX);
	test(rc == 0);

	machine_tls_t *tls;
	tls = machine_tls_create();
	rc = machine_tls_set_verify(tls, "none");
	test(rc == 0);
	rc = machine_tls_set_ca_file(tls, "./machinarium/ca.crt");
	test(rc == 0);
	rc = machine_tls_set_cert_file(tls, "./machinarium/client.crt");
	test(rc == 0);
	rc = machine_tls_set_key_file(tls, "./machinarium/client.key");
	test(rc == 0);
	rc = machine_set_tls(client, tls, UINT32_MAX);
	if (rc == -1) {
		printf("%s\n", machine_error(client));
		test(rc == 0);
	}

	machine_msg_t *msg;
	msg = machine_read(client, 12, UINT32_MAX);
	test(msg != NULL);
	test(memcmp(machine_msg_data(msg), "hello world", 12) == 0);
	machine_msg_free(msg);

	msg = machine_read(client, 1, UINT32_MAX);
	/* eof */
	test(msg == NULL);

	rc = machine_close(client);
	test(rc == 0);
	machine_io_free(client);

	machine_tls_free(tls);
}

static void test_cs(void *arg)
{
	unlink("_un_test");

	(void)arg;
	int rc;
	rc = machine_coroutine_create(server, NULL);
	test(rc != -1);

	rc = machine_coroutine_create(client, NULL);
	test(rc != -1);
}

void machinarium_test_tls_unix_socket(void)
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
