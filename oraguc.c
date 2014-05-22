#include "postgres.h"
#include "utils/guc.h"
#include "orafunc.h"
#include "builtins.h"

/*  default value */
char  *nls_date_format = NULL;

void
_PG_init(void)
{
	/* Define custom GUC variables. */
	DefineCustomStringVariable("orafce.nls_date_format",
									"Emulate oracle's date output behaviour.",
									NULL,
									&nls_date_format,
									NULL,
									PGC_USERSET,
									0,
#if PG_VERSION_NUM >= 90100
									NULL,
#endif
									NULL, NULL);
}
