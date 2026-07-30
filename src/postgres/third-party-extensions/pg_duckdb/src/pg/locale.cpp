#include "pgduckdb/pg/locale.hpp"

extern "C" {
#include "postgres.h"
}

/*
 * Needs to be done outside of 'extern "C"' block AND before including
 * pg_locale.h. Otherwise you get many compilation errors about templates being
 * used in C linkage. It also needs to be done after including postgres.h,
 * because otherwise USE_ICU does not exist.
 */
#ifdef USE_ICU
#include <unicode/ucol.h>
#endif

extern "C" {
#include "utils/pg_locale.h"
}

namespace pgduckdb::pg {

bool
IsCLocale(Oid collation_id) {
#if PG_VERSION_NUM >= 180000
	return pg_newlocale_from_collation(collation_id)->collate_is_c;
#else
	return lc_ctype_is_c(collation_id);
#endif
}

} // namespace pgduckdb::pg
