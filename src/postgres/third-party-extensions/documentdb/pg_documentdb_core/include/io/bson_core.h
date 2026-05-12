/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/io/bson_core.h
 *
 * Core declarations of the bson type.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_CORE_H
#define BSON_CORE_H

#define PRIVATE_PGBSON_H
#include "io/pgbson.h"
#include "io/pgbson_writer.h"
#undef PRIVATE_PGBSON_H
#define bson_t do_not_use_this_type

#include "io/pgbsonelement.h"
#include "utils/string_view.h"
#include "io/bsonvalue_utils.h"
#include "utils/documentdb_pg_compatibility.h"

#endif
