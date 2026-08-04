/*
 * variable.h
 *		YB-only declarations for specialized SET variable hooks.
 *
 * YB_TODO_PG19MERGE: PG19 (commit 0a20ff54f5e3f74e88d022dc8b2c5d537b0a78bc)
 * removed this file; the PG-side declarations moved to
 * src/include/utils/guc_hooks.h. YB retains a slimmed variable.h holding only
 * the YB-specific GUC hook declarations, included by guc.c, pg_yb_utils.c,
 * and postgres.c.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/variable.h
 */
#ifndef VARIABLE_H
#define VARIABLE_H

#include "utils/guc.h"

/* YB */
extern void yb_assign_XactIsoLevel(int newval, void *extra);
extern bool check_yb_default_xact_isolation(int *newval, void **extra, GucSource source);
extern void assign_transaction_read_only(bool newval, void *extra);
extern void assign_transaction_deferrable(bool newval, void *extra);
extern bool check_follower_reads(bool *newval, void **extra, GucSource source);
extern void assign_follower_reads(bool newval, void *extra);
extern bool check_follower_read_staleness_ms(int32_t *newval, void **extra, GucSource source);
extern void assign_follower_read_staleness_ms(int32_t newval, void *extra);
extern bool check_default_XactIsoLevel(int *newval, void **extra, GucSource source);
extern const char *yb_fetch_effective_transaction_isolation_level(void);

#endif							/* VARIABLE_H */
