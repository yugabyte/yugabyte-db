
#include <machinarium.h>
#include <odyssey.h>

void od_dbg_printf(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}
