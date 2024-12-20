/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/helio_bson_core.h
 *
 * Core declarations of the bson type.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_BSON_CORE_H
#define HELIO_BSON_CORE_H

#define PRIVATE_PGBSON_H
#include "io/pgbson.h"
#include "io/pgbson_writer.h"
#undef PRIVATE_PGBSON_H
#define bson_t do_not_use_this_type

#include "io/pgbsonelement.h"
#include "utils/string_view.h"
#include "io/bsonvalue_utils.h"

#endif
