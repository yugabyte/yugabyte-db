
#include <machinarium.h>
#include <odyssey.h>

void od_dbg_printf(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	// The following line was changed from upstream to use stdout
	// instead of stderr because of segmentation fault.
	// TODO(janand): Fix the segmentation fault received
	// in case of stderr. (GH #17586)
	vfprintf(stdout, fmt, args);
	va_end(args);
}
