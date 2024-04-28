/*-------------------------------------------------------------------------
 *
 * hypopg.h: Implementation of hypothetical indexes for PostgreSQL
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (C) 2015-2024: Julien Rouhaud
 *
 *-------------------------------------------------------------------------
*/
#ifndef _HYPOPG_H_
#define _HYPOPG_H_

#if PG_VERSION_NUM >= 120000
#include "access/table.h"
#endif
#include "catalog/catalog.h"
#include "commands/explain.h"
#include "nodes/nodeFuncs.h"
#include "utils/memutils.h"

#include "include/hypopg_import.h"

/* Provide backward compatibility macros for table.c API on pre v12 versions */
#if PG_VERSION_NUM < 120000
#define table_open(r, l)	heap_open(r, l)
#define table_close(r, l)	heap_close(r, l)
#endif

/*
 * Hacky macro to provide backward compatibility with either 1 or 2 arg lnext()
 * on pre v13 versions
 */
#if PG_VERSION_NUM < 130000
#define LNEXT(_1, _2, NAME, ...)	NAME
#undef lnext
#define lnext(...)			LNEXT(__VA_ARGS__, LNEXT2, LNEXT1) (__VA_ARGS__)
#define LNEXT1(lc)			((lc)->next)
#define LNEXT2(list, lc)	((lc)->next)
#endif

/* Backport of atooid macro */
#if PG_VERSION_NUM < 100000
#define atooid(x) ((Oid) strtoul((x), NULL, 10))
#endif

extern bool isExplain;

/* GUC for enabling / disabling hypopg during EXPLAIN */
extern bool hypo_is_enabled;
extern MemoryContext HypoMemoryContext;

Oid			hypo_getNewOid(Oid relid);
void		hypo_reset_fake_oids(void);

#endif
