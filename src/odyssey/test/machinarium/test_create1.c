
#include <machinarium.h>
#include <odyssey_test.h>

static int a = 0;
static int b = 0;
static int c = 0;

static void test_a(void *arg)
{
	(void)arg;
	a++;
}

static void test_b(void *arg)
{
	(void)arg;
	b++;
}

static void test_c(void *arg)
{
	(void)arg;
	c++;
}

void machinarium_test_create1(void)
{
	machinarium_init();

	int a_id;
	a_id = machine_create("a", test_a, NULL);
	test(a_id != -1);

	int b_id;
	b_id = machine_create("b", test_b, NULL);
	test(b_id != -1);

	int c_id;
	c_id = machine_create("c", test_c, NULL);
	test(c_id != -1);

	int rc;
	rc = machine_wait(a_id);
	test(rc != -1);
	test(a == 1);

	rc = machine_wait(b_id);
	test(rc != -1);
	test(b == 1);

	rc = machine_wait(c_id);
	test(rc != -1);
	test(c == 1);

	machinarium_free();
}
