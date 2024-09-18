//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//--------------------------------------------------------------------------------------------------

// This file contains thread-local equivalents for (some) PG global variables
// to allow (some components of) the PG/YSQL layer to run in a multi-threaded
// environment.
//
// Currently this is used within DocDB where PG/YSQL is used as a library for
// evaluating YSQL expression.

#pragma once

#include "yb/yql/pggate/ybc_pg_typedefs.h"
namespace yb::pggate {

//-----------------------------------------------------------------------------
// Memory context.
//-----------------------------------------------------------------------------

/* CurrentMemoryContext (from palloc.h/mcxt.c) */
void* PgGetThreadLocalCurrentMemoryContext();
void* PgSetThreadLocalCurrentMemoryContext(void *memctx);

/*
 * Reset all variables that would be allocated within the CurrentMemoryContext.
 * These should not be used anyway, but just keeping things tidy.
 * To be used when calling MemoryContextReset() for the CurrentMemoryContext.
 */
void PgResetCurrentMemCtxThreadLocalVars();

//-----------------------------------------------------------------------------
// Error reporting.
//-----------------------------------------------------------------------------

/*
 * Jump buffer used for error reporting (with sigjmp/longjmp) in multithread
 * context.
 * TODO Currently not yet the same as the standard PG/YSQL sigjmp_buf exception
 * stack because we use simplified error reporting when multi-threaded.
 */
void* PgSetThreadLocalJumpBuffer(void* new_buffer);
void* PgGetThreadLocalJumpBuffer();

void *PgSetThreadLocalErrStatus(void* new_status);
void *PgGetThreadLocalErrStatus();

//-----------------------------------------------------------------------------
// Expression processing.
//-----------------------------------------------------------------------------

/*
 * pg_strtok_ptr (from read.c)
 * TODO Technically this does not need to be global but refactoring the parsing
 * code to pass it as a parameter is tedious due to the notational overhead.
 */
void* PgGetThreadLocalStrTokPtr();
void PgSetThreadLocalStrTokPtr(char *new_pg_strtok_ptr);

/*
 * CachedRegexpHolder (from regex/regex.c)
 * These are used to cache the compiled regexes
 */
YBCPgThreadLocalRegexpCache* PgGetThreadLocalRegexpCache();
YBCPgThreadLocalRegexpCache* PgInitThreadLocalRegexpCache(
    size_t buffer_size, YBCPgThreadLocalRegexpCacheCleanup cleanup);

} // namespace yb::pggate
