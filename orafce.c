#include "postgres.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/guc.h"

#include "orafce.h"
#include "builtins.h"
#include "pipe.h"

/*  default value */
char  *nls_date_format = NULL;

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
}
