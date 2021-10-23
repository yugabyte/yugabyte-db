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

#include "postgres.h"

#include "access/attnum.h"
#include "access/htup.h"
#include "access/tupdesc.h"
#include "catalog/pg_type.h"
#include "common/keywords.h"
#include "fmgr.h"
#include "funcapi.h"

#include "parser/cypher_gram.h"

/*
 * This list must be sorted by ASCII name, because binary search is used to
 * locate entries.
 */
const ScanKeyword cypher_keywords[] = {
    {"analyze", ANALYZE, RESERVED_KEYWORD},
    {"and", AND, RESERVED_KEYWORD},
    {"as", AS, RESERVED_KEYWORD},
    {"asc", ASC, RESERVED_KEYWORD},
    {"ascending", ASCENDING, RESERVED_KEYWORD},
    {"by", BY, RESERVED_KEYWORD},
    {"case", CASE, RESERVED_KEYWORD},
    {"coalesce", COALESCE, RESERVED_KEYWORD},
    {"contains", CONTAINS, RESERVED_KEYWORD},
    {"create", CREATE, RESERVED_KEYWORD},
    {"delete", DELETE, RESERVED_KEYWORD},
    {"desc", DESC, RESERVED_KEYWORD},
    {"descending", DESCENDING, RESERVED_KEYWORD},
    {"detach", DETACH, RESERVED_KEYWORD},
    {"distinct", DISTINCT, RESERVED_KEYWORD},
    {"else", ELSE, RESERVED_KEYWORD},
    {"end", END_P, RESERVED_KEYWORD},
    {"ends", ENDS, RESERVED_KEYWORD},
    {"exists", EXISTS, RESERVED_KEYWORD},
    {"explain", EXPLAIN, RESERVED_KEYWORD},
    {"false", FALSE_P, RESERVED_KEYWORD},
    {"in", IN, RESERVED_KEYWORD},
    {"is", IS, RESERVED_KEYWORD},
    {"limit", LIMIT, RESERVED_KEYWORD},
    {"match", MATCH, RESERVED_KEYWORD},
    {"not", NOT, RESERVED_KEYWORD},
    {"null", NULL_P, RESERVED_KEYWORD},
    {"or", OR, RESERVED_KEYWORD},
    {"order", ORDER, RESERVED_KEYWORD},
    {"remove", REMOVE, RESERVED_KEYWORD},
    {"return", RETURN, RESERVED_KEYWORD},
    {"set", SET, RESERVED_KEYWORD},
    {"skip", SKIP, RESERVED_KEYWORD},
    {"starts", STARTS, RESERVED_KEYWORD},
    {"then", THEN, RESERVED_KEYWORD},
    {"true", TRUE_P, RESERVED_KEYWORD},
    {"verbose", VERBOSE, RESERVED_KEYWORD},
    {"when", WHEN, RESERVED_KEYWORD},
    {"where", WHERE, RESERVED_KEYWORD},
    {"with", WITH, RESERVED_KEYWORD},
    {"xor", XOR, RESERVED_KEYWORD}
};

const int num_cypher_keywords = lengthof(cypher_keywords);

PG_FUNCTION_INFO_V1(get_cypher_keywords);

// function to return the list of grammar keywords
Datum get_cypher_keywords(PG_FUNCTION_ARGS)
{
    FuncCallContext *func_ctx;

    if (SRF_IS_FIRSTCALL())
    {
        MemoryContext old_mem_ctx;
        TupleDesc tup_desc;

        func_ctx = SRF_FIRSTCALL_INIT();
        old_mem_ctx = MemoryContextSwitchTo(func_ctx->multi_call_memory_ctx);

        tup_desc = CreateTemplateTupleDesc(3, false);
        TupleDescInitEntry(tup_desc, (AttrNumber)1, "word", TEXTOID, -1, 0);
        TupleDescInitEntry(tup_desc, (AttrNumber)2, "catcode", CHAROID, -1, 0);
        TupleDescInitEntry(tup_desc, (AttrNumber)3, "catdesc", TEXTOID, -1, 0);

        func_ctx->attinmeta = TupleDescGetAttInMetadata(tup_desc);

        MemoryContextSwitchTo(old_mem_ctx);
    }

    func_ctx = SRF_PERCALL_SETUP();

    if (func_ctx->call_cntr < num_cypher_keywords)
    {
        char *values[3];
        HeapTuple tuple;

        // cast-away-const is ugly but alternatives aren't much better
        values[0] = (char *)cypher_keywords[func_ctx->call_cntr].name;

        switch (cypher_keywords[func_ctx->call_cntr].category)
        {
        case UNRESERVED_KEYWORD:
            values[1] = "U";
            values[2] = "unreserved";
            break;
        case COL_NAME_KEYWORD:
            values[1] = "C";
            values[2] = "unreserved (cannot be function or type name)";
            break;
        case TYPE_FUNC_NAME_KEYWORD:
            values[1] = "T";
            values[2] = "reserved (can be function or type name)";
            break;
        case RESERVED_KEYWORD:
            values[1] = "R";
            values[2] = "reserved";
            break;
        default:
            // shouldn't be possible
            values[1] = NULL;
            values[2] = NULL;
            break;
        }

        tuple = BuildTupleFromCStrings(func_ctx->attinmeta, values);

        SRF_RETURN_NEXT(func_ctx, HeapTupleGetDatum(tuple));
    }

    SRF_RETURN_DONE(func_ctx);
}
