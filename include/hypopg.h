/*-------------------------------------------------------------------------
 *
 * hypopg.h: Implementation of hypothetical indexes for PostgreSQL
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (C) 2015-2018: Julien Rouhaud
 *
 *-------------------------------------------------------------------------
*/
#ifndef _HYPOPG_H_
#define _HYPOPG_H_

#include "catalog/catalog.h"
#include "commands/explain.h"
#include "nodes/nodeFuncs.h"
#include "utils/memutils.h"

#include "include/hypopg_import.h"

extern bool isExplain;

/* GUC for enabling / disabling hypopg during EXPLAIN */
extern bool hypo_is_enabled;
extern MemoryContext HypoMemoryContext;

Oid			hypo_getNewOid(Oid relid);

#endif
