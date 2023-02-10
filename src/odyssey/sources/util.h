#ifndef ODYSSEY_UTIL_H
#define ODYSSEY_UTIL_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

static inline int od_vsnprintf(char *buf, int size, char *fmt, va_list args)
{
	int rc;
	rc = vsnprintf(buf, size, fmt, args);
	if (od_unlikely(rc >= size))
		rc = (size - 1);
	return rc;
}

static inline int od_vasprintf(char **__restrict bufp, char *fmt, va_list args)
{
	vasprintf(bufp, fmt, args);

	if (*bufp == NULL) {
		return NOT_OK_RESPONSE;
	}

	return OK_RESPONSE;
}

static inline int od_asprintf(char **__restrict bufp, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int rc = vasprintf(bufp, fmt, args);
	va_end(args);
	if (rc == -1) {
		return NOT_OK_RESPONSE;
	}

	if (*bufp == NULL) {
		return NOT_OK_RESPONSE;
	}

	return OK_RESPONSE;
}

static inline int od_snprintf(char *buf, int size, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int rc;
	rc = od_vsnprintf(buf, size, fmt, args);
	va_end(args);
	return rc;
}

static inline char *od_strdup_from_buf(const char *source, size_t size)
{
	char *str = malloc(size + 1);
	memcpy(str, source, size);
	str[size] = '\0';
	return str;
}

static inline long od_memtol(char *data, size_t data_size, char **end_ptr,
			     int base)
{
	// Only 10 is supported
	if (base != 10)
		abort();

	size_t i = 0;
	while (i < data_size && isspace(data[i]))
		i++;
	if (i >= data_size)
		return 0;

	char sign = data[i];
	if (sign == '-' || sign == '+')
		i++;
	if (i >= data_size || !isdigit(data[i]))
		return 0;

	long result = 0;
	while (i < data_size && isdigit(data[i])) {
		result = result * 10 + (data[i] - '0');
		i++;
	}

	if (i < data_size && !isspace(data[i]))
		return 0;

	if (end_ptr)
		*end_ptr = data + i;

	if (sign == '-')
		return -result;
	return result;
}

#endif /* ODYSSEY_UTIL_H */
