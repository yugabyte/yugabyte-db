/*-------------------------------------------------------------------------
 *
 * nodeYbBitmapTablescan.h
 *
 * Copyright (c) 2024 Yugabyte, Inc
 *
 * src/include/executor/nodeYbBitmapTablescan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEYBBITMAPTABLESCAN_H
#define NODEYBBITMAPTABLESCAN_H

#include "nodes/execnodes.h"

extern YbBitmapTableScanState *ExecInitYbBitmapTableScan(YbBitmapTableScan *node,
                                                         EState *estate, int eflags);
extern void ExecEndYbBitmapTableScan(YbBitmapTableScanState *node);
extern void ExecReScanYbBitmapTableScan(YbBitmapTableScanState *node);

#endif							/* NODEYBBITMAPTABLESCAN_H */
