
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <machinarium.h>
#include <odyssey.h>

static int read_attribute_buf_after_key(char **data, size_t *data_size,
					char **out, size_t *out_size)
{
	char *data_end = *data + *data_size;
	char *begin = *data;
	char *end;

	if (begin >= data_end || *begin != '=')
		return -1;

	begin++;

	end = begin;

	while (end < data_end && *end && *end != ',')
		end++;

	if (end < data_end) {
		*data = end + 1;
	} else {
		*data = end;
	}
	*data_size = data_end - *data;

	if (out)
		*out = begin;
	if (out_size)
		*out_size = end - begin;

	return 0;
}

int read_attribute_buf(char **data, size_t *data_size, char attr_key,
		       char **out, size_t *out_size)
{
	if (!*data_size || **data != attr_key)
		return -1;

	char *new_data = *data + 1;
	size_t new_data_size = *data_size - 1;

	if (read_attribute_buf_after_key(&new_data, &new_data_size, out,
					 out_size) == -1)
		return -1;

	*data = new_data;
	*data_size = new_data_size;

	return 0;
}

int read_any_attribute_buf(char **data, size_t *data_size, char *attribute_ptr,
			   char **out, size_t *out_size)
{
	if (!*data_size)
		return -1;
	char attribute = **data;

	if (!isalpha(attribute))
		return -1;

	if (attribute_ptr != NULL)
		*attribute_ptr = attribute;

	char *new_data = *data + 1;
	size_t new_data_size = *data_size - 1;

	if (read_attribute_buf_after_key(&new_data, &new_data_size, out,
					 out_size) == -1)
		return -1;

	*data = new_data;
	*data_size = new_data_size;

	return 0;
}
