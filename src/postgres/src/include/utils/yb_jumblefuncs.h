/*-------------------------------------------------------------------------
 *
 * yb_jumblefuncs.h
 *	  Helper functions for calculating a plan ID through a "jumble".
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/include/utils/yb_jumblefuncs.h
 *
 *-------------------------------------------------------------------------
 */

 /*
  * YB: Source of this file is the file jumblefuncs.h in the pg_stat_plans v2.0.0 extension
  *     (commit 1a86703).
  */
#ifndef YB_JUMBLEFUNCS_H
#define YB_JUMBLEFUNCS_H

#include "nodes/parsenodes.h"
#include "nodes/pathnodes.h"
#include "utils/yb_queryjumble.h"

extern YbJumbleState *YbInitJumble(void);
extern void YbJumbleNode(YbJumbleState *jstate, Node *node);
extern uint64 YbHashJumbleState(YbJumbleState *jstate);

/* Plan jumbling routines */
extern void YbJumbleRangeTableList(YbJumbleState *jstate, List *rtable);
extern void YbJumbleList(YbJumbleState *jstate, List *list, bool symmetric);


#endif							/* YB_JUMBLEFUNCS_H */
