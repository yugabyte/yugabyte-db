#pragma once

#include <inttypes.h>

/*
This file contains a few Postgres declarations.

This is meant to be used in files that are mostly C++, but
need to interact with Postgres C code (eg. catalog implementation).

It should not include any C++ code, only Postgres C declarations.
*/

extern "C" {
typedef int16_t AttrNumber;

typedef uint32_t BlockNumber;

typedef int Buffer;

struct BufferAccessStrategyData;
typedef struct BufferAccessStrategyData *BufferAccessStrategy;

typedef double Cardinality;

typedef uintptr_t Datum;

struct MemoryContextData;
typedef MemoryContextData *MemoryContext;

struct FormData_pg_attribute;
typedef FormData_pg_attribute *Form_pg_attribute;

struct FormData_pg_class;
typedef FormData_pg_class *Form_pg_class;

struct HeapTupleData;
typedef HeapTupleData *HeapTuple;

struct List;

struct Node;

typedef uint16_t OffsetNumber;

typedef unsigned int Oid;

struct ParamListInfoData;
typedef struct ParamListInfoData *ParamListInfo;

struct PlannedStmt;

typedef char *Pointer;
typedef Pointer Page;

struct Query;

struct RelationData;
typedef struct RelationData *Relation;

struct SnapshotData;
typedef struct SnapshotData *Snapshot;

struct TupleDescData;
typedef struct TupleDescData *TupleDesc;

struct TupleTableSlot;

struct TableAmRoutine;

typedef uint32_t CommandId;

typedef uint32_t SubTransactionId;

struct QueryDesc;

struct ParallelExecutorInfo;

struct MinimalTupleData;
typedef MinimalTupleData *MinimalTuple;

struct TupleQueueReader;

struct ObjectAddress;

struct PlanState;

struct Plan;

struct FuncExpr;

typedef struct FunctionCallInfoBaseData *FunctionCallInfo;

struct ExplainState;
}
