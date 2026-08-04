#ifndef ODYSSEY_ERROR_H
#define ODYSSEY_ERROR_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_error od_error_t;

#define OD_ERROR_MAX_LEN 256

struct od_error {
	char file[OD_ERROR_MAX_LEN];
	int file_len;
	char function[128];
	int function_len;
	char error[OD_ERROR_MAX_LEN];
	int error_len;
	int line;
};

static inline void od_error_init(od_error_t *error)
{
	error->file[0] = 0;
	error->file_len = 0;
	error->function[0] = 0;
	error->function_len = 0;
	error->error[0] = 0;
	error->error_len = 0;
	error->line = 0;
}

static inline void od_error_setv(od_error_t *error, const char *file,
				 const char *function, int line, char *fmt,
				 va_list args)
{
	error->file_len =
		od_snprintf(error->file, sizeof(error->file), "%s", file);
	error->function_len = od_snprintf(
		error->function, sizeof(error->function), "%s", function);
	error->line = line;
	int len;
	len = od_vsnprintf(error->error, sizeof(error->error), fmt, args);
	error->error_len = len;
}

static inline int od_error_set(od_error_t *error, const char *file,
			       const char *function, int line, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_error_setv(error, file, function, line, fmt, args);
	va_end(args);
	return -1;
}

#define od_errorf(error, fmt, ...) \
	od_error_set(error, __FILE__, __func__, __LINE__, fmt, __VA_ARGS__)

#endif /* ODYSSEY_ERROR_H */
