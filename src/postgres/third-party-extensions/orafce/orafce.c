#include "postgres.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/guc.h"

#if PG_VERSION_NUM >=  160000

#include "utils/guc_hooks.h"

#else

#include "commands/variable.h"

#endif

#include "orafce.h"
#include "builtins.h"
#include "pipe.h"

#if PG_VERSION_NUM >= 150000

#include "miscadmin.h"

#endif

/*  default value */
char  *nls_date_format = NULL;
char  *orafce_timezone = NULL;

extern char *orafce_sys_guid_source;

static const struct config_enum_entry orafce_compatibility_options[] = {
	{"warning_oracle", ORAFCE_COMPATIBILITY_WARNING_ORACLE, false},
	{"warning_orafce", ORAFCE_COMPATIBILITY_WARNING_ORAFCE, false},
	{"oracle", ORAFCE_COMPATIBILITY_ORACLE, false},
	{"orafce", ORAFCE_COMPATIBILITY_ORAFCE, false},
	{NULL, 0, false}
};

#if PG_VERSION_NUM >= 150000

static shmem_request_hook_type prev_shmem_request_hook = NULL;

#endif

#if PG_VERSION_NUM >= 150000

static void
orafce_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	RequestAddinShmemSpace(SHMEMMSGSZ);
}

#endif

static bool
check_sys_guid_source(char **newval, void **extra, GucSource source)
{
	char	   *value = *newval;
	const char *canonicalstr;
	char	   *result;

	if (pg_strcasecmp(value, "uuid_generate_v1") == 0)
		canonicalstr = "uuid_generate_v1";
	else if (pg_strcasecmp(value, "uuid_generate_v1mc") == 0)
		canonicalstr = "uuid_generate_v1mc";
	else if (pg_strcasecmp(value, "uuid_generate_v4") == 0)
		canonicalstr = "uuid_generate_v1";
	else if (pg_strcasecmp(value, "gen_random_uuid") == 0)
		canonicalstr = "gen_random_uuid";
	else
		return false;

#if PG_VERSION_NUM >= 160000

	result = (char *) guc_malloc(LOG, 32);
	if (!result)
		return false;

	strcpy(result, canonicalstr);
	guc_free(*newval);
	*newval = result;

#else

	result = (char *) malloc(32);
	if (!result)
		return false;

	strcpy(result, canonicalstr);
	free(*newval);
	*newval = result;

#endif

	return true;
}


void
_PG_init(void)
{

#if PG_VERSION_NUM >= 150000

	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = orafce_shmem_request;

#else

	RequestAddinShmemSpace(SHMEMMSGSZ);

#endif

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
									check_timezone, NULL, NULL);

	DefineCustomBoolVariable("orafce.varchar2_null_safe_concat",
									"Specify timezone used for sysdate function.",
									NULL,
									&orafce_varchar2_null_safe_concat,
									false,
									PGC_USERSET,
									0,
									NULL, NULL, NULL);

	DefineCustomStringVariable("orafce.sys_guid_source",
									"Specify function from uuid-ossp extension used for making result.",
									NULL,
									&orafce_sys_guid_source,
									"uuid_generate_v1",
									PGC_USERSET,
									0,
									check_sys_guid_source, NULL, NULL);

	DefineCustomEnumVariable("orafce.using_substring_zero_width_in_substr",
							 gettext_noop("behaviour of substr function when substring_length argument is zero"),
							 NULL,
							 &orafce_substring_length_is_zero,
							 ORAFCE_COMPATIBILITY_WARNING_ORACLE,
							 orafce_compatibility_options,
							 PGC_USERSET, 0,
							 NULL, NULL, NULL);


	EmitWarningsOnPlaceholders("orafce");

	RegisterXactCallback(orafce_xact_cb, NULL);
}
