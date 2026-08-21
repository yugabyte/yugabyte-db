/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef AG_GRAPH_COMMANDS_H
#define AG_GRAPH_COMMANDS_H

/* YB includes */
#include "pg_yb_utils.h"

Datum create_graph(PG_FUNCTION_ARGS);
Oid create_graph_internal(const Name graph_name);

/*
 * Macros to wrap DDL operations in YugabyteDB DDL transactions.
 * This deduplicates the transaction control logic across graph and label
 * DDL commands, and preserves original indentation of the DDL code to
 * make future upstream merges easier.
 */
#define YB_BEGIN_TRANSACTIONAL_DDL() \
    do { \
        bool yb_use_regular_txn_block_ = YBIsDdlTransactionBlockEnabled(); \
        if (IsYugaByteEnabled()) \
        { \
            if (yb_use_regular_txn_block_) \
                YBAddDdlTxnState(YB_DDL_MODE_VERSION_INCREMENT); \
            else \
                YBIncrementDdlNestingLevel(YB_DDL_MODE_VERSION_INCREMENT); \
        } \
        PG_TRY(); \
        {

#define YB_END_TRANSACTIONAL_DDL() \
        } \
        PG_FINALLY(); \
        { \
            if (IsYugaByteEnabled()) \
            { \
                if (yb_use_regular_txn_block_) \
                    YBMergeDdlTxnState(); \
                else \
                    YBDecrementDdlNestingLevel(); \
            } \
        } \
        PG_END_TRY(); \
    } while(0)

#endif
