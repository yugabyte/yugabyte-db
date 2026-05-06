#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb {

void InitBackgroundWorkersShmem(void);
void StartBackgroundWorkerIfNeeded(void);
void TriggerActivity(void);
void UnclaimBgwSessionHint(int code = 0, Datum arg = 0);
const char *PossiblyReuseBgwSessionHint(void);

extern bool is_background_worker;
extern bool doing_motherduck_sync;
extern char *current_duckdb_database_name;
extern char *current_motherduck_catalog_version;

} // namespace pgduckdb
