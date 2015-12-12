#include "postgres.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/guc.h"
#include "commands/variable.h"

#include "orafce.h"
#include "builtins.h"
#include "pipe.h"

/*  default value */
char  *nls_date_format = NULL;
char  *orafce_timezone = NULL;

void
_PG_init(void)
{
	EmitWarningsOnPlaceholders("orafce");

	RequestAddinShmemSpace(SHMEMMSGSZ);
	RequestAddinLWLocks(1);

	/* Define custom GUC variables. */
	DefineCustomStringVariable("orafce.nls_date_format",
									"Emulate oracle's date output behaviour.",
									NULL,
									&nls_date_format,
									NULL,
									PGC_USERSET,
									0,
									NULL,
									NULL, NULL);

	DefineCustomStringVariable("orafce.timezone",
									"Specify timezone used for sysdate function.",
									NULL,
									&orafce_timezone,
									"GMT",
									PGC_USERSET,
									0,
									check_timezone, NULL, show_timezone);
}
