/*-------------------------------------------------------------------------
 *
 * nodeYbBatchedNestLoop.h
 *	  Implementation of Yugabyte's batched nested loop join.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/postgres/src/include/executor/nodeYbBatchedNestloop.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef NODEYBBATCHEDNESTLOOP_H
#define NODEYBBATCHEDNESTLOOP_H

#include "nodes/execnodes.h"


extern YbBatchedNestLoopState *ExecInitYbBatchedNestLoop(YbBatchedNestLoop *node, EState *estate, int eflags);
extern void ExecEndYbBatchedNestLoop(YbBatchedNestLoopState *node);
extern void ExecReScanYbBatchedNestLoop(YbBatchedNestLoopState *node);

#endif							/* NODEYBBATCHEDNESTLOOP_H */
