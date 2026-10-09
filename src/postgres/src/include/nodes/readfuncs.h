/*-------------------------------------------------------------------------
 *
 * readfuncs.h
 *	  header file for read.c and readfuncs.c. These functions are internal
 *	  to the stringToNode interface and should not be used by anyone else.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/nodes/readfuncs.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef READFUNCS_H
#define READFUNCS_H

#include "nodes/nodes.h"

/* YB includes */
#ifdef DEBUG_NODE_TESTS_ENABLED
#include "pg_yb_utils.h"

/*
 * variable in read.c that needs to be accessible to readfuncs.c
 */
/*
 * YB: per-thread. ybDeserializeNode runs on concurrent tserver threads, and
 * pg_strtok's pointer is already per-thread.
 */
extern PGDLLIMPORT YB_THREAD_LOCAL bool restore_location_fields;
#endif

/*
 * prototypes for functions in read.c (the lisp token parser)
 */
extern const char *pg_strtok(int *length);
extern char *debackslash(const char *token, int length);
extern void *nodeRead(const char *token, int tok_len);

/*
 * prototypes for functions in readfuncs.c
 */
extern Node *parseNodeString(void);

extern int	GetYbExpressionVersion(void);

#endif							/* READFUNCS_H */
