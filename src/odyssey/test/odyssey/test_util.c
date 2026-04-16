
#include "odyssey.h"

void test_od_memtol_sanity()
{
	char str[] = " \t  42 +12\t-17   -0  +0";
	size_t data_size = strlen(str);
	char *data = malloc(data_size);
	memcpy(data, str, data_size);

	char *ptr = data;
	long value;

	value = od_memtol(ptr, data_size, &ptr, 10);
	assert(value == 42);
	assert(memcmp(data, " \t  42", ptr - data) == 0);

	value = od_memtol(ptr, data_size - (ptr - data), &ptr, 10);
	assert(value == 12);
	assert(memcmp(data, " \t  42 +12", ptr - data) == 0);

	value = od_memtol(ptr, data_size - (ptr - data), &ptr, 10);
	assert(value == -17);
	assert(memcmp(data, " \t  42 +12\t-17", ptr - data) == 0);

	value = od_memtol(ptr, data_size - (ptr - data), &ptr, 10);
	assert(value == 0);
	assert(memcmp(data, " \t  42 +12\t-17   -0", ptr - data) == 0);

	value = od_memtol(ptr, data_size - (ptr - data), &ptr, 10);
	assert(value == 0);
	assert(memcmp(data, " \t  42 +12\t-17   -0  +0", ptr - data) == 0);

	free(data);
}

void odyssey_test_util(void)
{
	test_od_memtol_sanity();
}
