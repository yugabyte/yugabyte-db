/*-----------------------------------------------------------------------------
 *
 * yb_fdw_pggate_api.h
 *		  A single header to import YugaByte "PgGate" API so we can interact
 *		  with the YugaByte universe from PostgreSQL C code
 *
 * Copyright (c) YugaByte, Inc.
 *
 * IDENTIFICATION
 *		  contrib/yb_fdw/yb_fdw_pggate_api.h.c
 *
 *-----------------------------------------------------------------------------
 */

#ifndef YB_FDW_PGGATE_API_H
#define YB_FDW_PGGATE_API_H

#include "yb/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

// Needed for HandleYBStatus
#include "pg_yb_utils.h"

// The sequence below defines inline functions wrapping YB pggate C functions
// returning YBCStatus (their names are sufficed with _Status).
#include "yb/yql/pggate/if_macros_c_pg_wrapper_inl.h"
#include "yb/yql/pggate/pggate_if.h"
#include "yb/yql/pggate/if_macros_undef.h"

#endif  // YB_FDW_PGGATE_API_H
