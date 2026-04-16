/*-------------------------------------------------------------------------
 *
 * nodeYbBitmapIndexscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeYbBitmapIndexscan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEYBBITMAPINDEXSCAN_H
#define NODEYBBITMAPINDEXSCAN_H

#include "nodes/execnodes.h"

extern YbBitmapIndexScanState *ExecInitYbBitmapIndexScan(YbBitmapIndexScan *node, EState *estate, int eflags);
extern Node *MultiExecYbBitmapIndexScan(YbBitmapIndexScanState *node);
extern void ExecEndYbBitmapIndexScan(YbBitmapIndexScanState *node);
extern void ExecReScanYbBitmapIndexScan(YbBitmapIndexScanState *node);

#endif							/* NODEYBBITMAPINDEXSCAN_H */
