#ifndef MM_UTIL_H
#define MM_UTIL_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

static inline int mm_vsnprintf(char *buf, int size, char *fmt, va_list args)
{
	int rc;
	rc = vsnprintf(buf, size, fmt, args);
	if (rc >= size)
		rc = (size - 1);
	return rc;
}

static inline int mm_snprintf(char *buf, int size, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int rc;
	rc = mm_vsnprintf(buf, size, fmt, args);
	va_end(args);
	return rc;
}

#endif /* MM_UTIL_H */
