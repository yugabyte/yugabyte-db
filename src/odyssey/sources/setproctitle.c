
#include <machinarium.h>
#include <odyssey.h>

od_retcode_t od_setproctitlef(char **argv_ptr, char *fmt, ...)
{
	char title[OD_MAX_PROC_TITLE_LEN];
	va_list args;
	va_start(args, fmt);
	size_t title_len = od_vsnprintf(title, sizeof(title), fmt, args);
	va_end(args);

	/* dirty hack */
	title[title_len] = '\0';

	memcpy(*argv_ptr, title, title_len + 1);
	return OK_RESPONSE;
}
