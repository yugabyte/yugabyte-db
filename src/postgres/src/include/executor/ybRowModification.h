/*-------------------------------------------------------------------------
 *
 * ybRowModification.h
 *	  Command ids of the YB rows a statement modifies, YB's counterpart of
 *	  the cmax of a heap tuple.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/executor/ybRowModification.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_ROW_MODIFICATION_H
#define YB_ROW_MODIFICATION_H

#include "access/tableam.h"
#include "nodes/execnodes.h"
#include "utils/rel.h"

extern void YbBeginRowModifications(ModifyTableState *mtstate);
extern void YbEndRowModifications(ModifyTableState *mtstate);
extern void YbRecordRowModification(ModifyTableState *mtstate, Relation rel,
									TupleTableSlot *slot, bool deleted);
extern TM_Result YbGetRowModificationResult(EState *estate, Relation rel,
											TupleTableSlot *planSlot,
											TM_FailureData *tmfd);
extern bool YbCheckTupleForTrigger(EState *estate, Relation rel,
								   HeapTuple trigtuple, TM_Result *tmresult,
								   TM_FailureData *tmfd);
extern void YbCheckOnConflictRowNotModified(EState *estate, Relation rel,
											Datum ybctid);
extern bool YbIsRowDeletedByCommand(EState *estate, Relation rel,
									TupleTableSlot *slot);

#endif							/* YB_ROW_MODIFICATION_H */
