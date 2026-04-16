
#include "odyssey.h"

void test_read_attribute_buf_sanity()
{
	char str[] = "a=qwerty,b=,c=other";
	char *data = malloc(strlen(str));
	size_t ptr_size = strlen(str);
	memcpy(data, str, ptr_size);
	char *ptr = data;

	char *value;
	size_t value_size;
	assert(read_attribute_buf(&ptr, &ptr_size, 'a', &value, &value_size) ==
	       0);
	assert(memcmp(value, "qwerty", value_size) == 0);
	assert(read_attribute_buf(&ptr, &ptr_size, 'b', &value, &value_size) ==
	       0);
	assert(memcmp(value, "", value_size) == 0);
	assert(read_attribute_buf(&ptr, &ptr_size, 'c', &value, &value_size) ==
	       0);
	assert(memcmp(value, "other", value_size) == 0);

	free(data);
}

void test_read_any_attribute_buf_sanity()
{
	char str[] = "a=qwerty,b=,c=other";
	size_t data_size = strlen(str);
	char *data = malloc(data_size);
	memcpy(data, str, data_size);

	{
		char *ptr = data;
		size_t ptr_size = data_size;
		char *value;
		size_t value_size;
		char attribute;
		assert(read_any_attribute_buf(&ptr, &ptr_size, &attribute,
					      &value, &value_size) == 0);
		assert(memcmp(value, "qwerty", value_size) == 0);
		assert(attribute == 'a');
		assert(read_any_attribute_buf(&ptr, &ptr_size, &attribute,
					      &value, &value_size) == 0);
		assert(memcmp(value, "", value_size) == 0);
		assert(attribute == 'b');
		assert(read_any_attribute_buf(&ptr, &ptr_size, &attribute,
					      &value, &value_size) == 0);
		assert(memcmp(value, "other", value_size) == 0);
		assert(attribute == 'c');
	}

	{
		char *ptr = data;
		size_t ptr_size = data_size;
		char *value;
		size_t value_size;
		assert(read_any_attribute_buf(&ptr, &ptr_size, NULL, &value,
					      &value_size) == 0);
		assert(memcmp(value, "qwerty", value_size) == 0);
		assert(read_any_attribute_buf(&ptr, &ptr_size, NULL, &value,
					      &value_size) == 0);
		assert(memcmp(value, "", value_size) == 0);
		assert(read_any_attribute_buf(&ptr, &ptr_size, NULL, &value,
					      &value_size) == 0);
		assert(memcmp(value, "other", value_size) == 0);
	}

	free(data);
}

void odyssey_test_attribute(void)
{
	test_read_attribute_buf_sanity();
	test_read_any_attribute_buf_sanity();
}
