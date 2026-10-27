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

#include "access/skey.h"
#include "nodes/execnodes.h"

extern YbBitmapIndexScanState *ExecInitYbBitmapIndexScan(YbBitmapIndexScan *node, EState *estate, int eflags);
extern Node *MultiExecYbBitmapIndexScan(YbBitmapIndexScanState *node);
extern void ExecEndYbBitmapIndexScan(YbBitmapIndexScanState *node);
extern void ExecReScanYbBitmapIndexScan(YbBitmapIndexScanState *node);
extern bool yb_bitmap_index_might_recheck(Scan *scan, Relation heapRelation,
										  Relation indexRelation,
										  ScanKey keys, int nkeys,
										  int nruntimekeys);

#endif							/* NODEYBBITMAPINDEXSCAN_H */
