#pragma once

extern "C" {
#include "postgres.h"
}

namespace pgduckdb {

Datum DetoastPostgresDatum(struct varlena *value, bool *should_free);

} // namespace pgduckdb
