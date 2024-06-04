
#include <machinarium.h>
#include <odyssey_test.h>

static int gai_complete = 0;

static void test_gai_coroutine(void *arg)
{
	(void)arg;
	struct addrinfo *res = NULL;
	int rc = machine_getaddrinfo("localhost", "http", NULL, &res,
				     UINT32_MAX);
	if (rc < 0) {
		printf("failed to resolve address\n");
	} else {
		test(res != NULL);
		if (res)
			freeaddrinfo(res);
	}
	gai_complete++;
}

static void test_gai(void *arg)
{
	(void)arg;
	int rc;
	int workers[100];
	int i;
	for (i = 0; i < 100; i++) {
		rc = machine_coroutine_create(test_gai_coroutine, NULL);
		test(rc != -1);
		workers[i] = rc;
	}
	for (i = 0; i < 100; i++) {
		machine_join(workers[i]);
	}
	test(gai_complete == 100);
}

void machinarium_test_getaddrinfo2(void)
{
	machinarium_init();

	int id;
	id = machine_create("test", test_gai, NULL);
	test(id != -1);

	int rc;
	rc = machine_wait(id);
	test(rc != -1);

	machinarium_free();
}
