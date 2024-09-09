/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_densify.c
 *
 * Aggregation implementations of BSON densify stage.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <nodes/execnodes.h>
#include <miscadmin.h>
#include <parser/parse_clause.h>
#include <parser/parse_node.h>
#include <windowapi.h>
#include <utils/datetime.h>

#include "aggregation/bson_project.h"
#include "commands/parse_error.h"
#include "infrastructure/helio_external_configs.h"
#include "io/helio_bson_core.h"
#include "io/bson_hash.h"
#include "metadata/metadata_cache.h"
#include "query/bson_dollar_operators.h"
#include "query/helio_bson_compare.h"
#include "utils/hashset_utils.h"
#include "utils/fmgr_utils.h"
#include "utils/feature_counter.h"
#include "utils/helio_errors.h"
#include "utils/date_utils.h"

#include "aggregation/bson_densify.h"

/* GUC to enable $densify aggregation stage */
extern bool EnableDensifyStage;

/* Enum to represent the type of Densify */
typedef enum DensifyType
{
	DENSIFY_TYPE_INVALID,
	DENSIFY_TYPE_RANGE,
	DENSIFY_TYPE_PARTITION,
	DENSIFY_TYPE_FULL
} DensifyType;

/* Enum that represents whats the last row value for densify window */
typedef enum DensifyDocumentType
{
	DENSIFY_DOCUMENT_UNKNOWN,

	/* A null document */
	DENSIFY_DOCUMENT_NULL,

	/* A valid document */
	DENSIFY_DOCUMENT_VALID
} DensifyDocumentType;

/*
 * PartitionAwareState is a struct that holds the state of that is local
 * to the partition and is not shared across partitions.
 * This can be free'd once we switch partitions
 */
typedef struct PartitionAwareState
{
	/*
	 * Flags if the state is initialized for a new partition
	 */
	bool isInitialized;
	bool reset;

	/* Evaluated expression of partitionByField value on the document
	 * This is not available when the mode is `full` because there is no
	 * specific partition created and the partition related information
	 * would be stored in `partitionInfo` hashtbl.
	 */
	pgbson *evaluatedPartitionBy;

	/*
	 * The last minimum value till where the densification is done,
	 * exclusive of this value
	 */
	bson_value_t lastMinValue;

	/* is this the last row in the partition ? */
	bool isLastRow;

	/* The bool tells if we have already found a row which is out of bound and rest
	 * of the rows are guaranteed to be out of the range, so start skipping
	 * any comparision and densification effort
	 */
	bool skipRows;

	/*
	 * Only available when the partition mode is `full`. Stores
	 * the minimum values for each partition till where densification is done.
	 */
	HTAB *partitionInfo;
} PartitionAwareState;


/*
 *  Arguments of densify spec
 */
typedef struct DensifyArguments
{
	/* Densify type can be one of - range, partition or full */
	DensifyType densifyType;

	/* Field value for which densification will be done */
	StringView field;

	/* Step value to increment for densification */
	bson_value_t step;

	/* Start and end values when the type is range, empty for other types */
	bson_value_t lowerBound;
	bson_value_t upperBound;

	/* Time interval units and interval datum if units present */
	DateUnit timeUnit;

	/* partitionByField values for densify, these are validated and stored as
	 * a path tree so that the documents can just projected using the tree
	 */
	pgbson *partitionByFields;
} DensifyArguments;


/*
 * Hash entry for densify `full` mode
 * hash table. Holds the group key value and the
 * running lastMinValue from where next densification
 * will start for this group
 */
typedef struct DensifyFullEntry
{
	/* Evaluated partition by for this entry */
	pgbson *evaluatedPartitionBy;

	/* last minValue in group */
	bson_value_t lastMinValueInGroup;
} DensifyFullEntry;


/*
 * Holds either the step as bson_value_t
 * or as Interval is step is provided with a unit
 */
typedef union TypedStep
{
	bson_value_t numericStep;
	Datum intervalStep;
} TypedStep;


/*
 * Increments the baseValue with Step and stores back the result in baseValue
 */
typedef void (*StepIncrementorFunc)(bson_value_t *baseValue, TypedStep *step);

/*
 * State for the window functions to handle densification.
 * This is generally stored in the Executor context and common for all partitions.
 * There is a `partitionAwareState` which holds data specific to a parition and
 * is reset / cleared once the partition switch happens
 */
typedef struct DensifyWindowState
{
	/* $densify spec arguments */
	DensifyArguments arguments;

	/*
	 * The number of documents generated across all the
	 * partitions. This is used to enforce limits with parameter `internalQueryMaxAllowedDensifyDocs`
	 */
	int32 nDocumentsGenerated;
	int memConsumed;

	/*
	 * Type of the document for previous documents
	 */
	DensifyDocumentType previousType;

	/*
	 * Projection tree root node if `partitionByFields` is present
	 * This handles getting the values from document for partition
	 * from nested fields.
	 */
	BsonProjectionQueryState *partitionByTreeState;

	/*
	 * State fields that are responsible for choosing how to increment
	 * the step based on if the value is numeric or date.
	 */
	TypedStep typedStep;
	StepIncrementorFunc incrementor;

	/*
	 * Memory context refrence to the `Executor` state which usually outlives the
	 * partition level memory context and stays till the duration of query execution
	 */
	MemoryContext executorContext;

	/*
	 * This holds data local to partition and is usually resetted at the end when
	 * a partition is done processing/switched.
	 */
	PartitionAwareState partitionAwareState;
} DensifyWindowState;


static const char *DENSIFY_RESULT_FIELD = "_";
static const int DENSIFY_RESULT_FIELD_LENGTH = 1;

/*====================================*/
/* Forward declarations               */
/*====================================*/

static inline void CheckFieldValue(const bson_value_t *fieldValue,
								   DensifyWindowState *state);
static inline void AddGroupByValueToGeneratedDocuments(pgbson_writer *writer, const
													   pgbson *partitionBy);
static inline pgbson * GetPartitionByFieldsPgbson(pgbson *document,
												  DensifyWindowState *state,
												  bool moveToExecutorContext);
static inline void ReleasePartitionAwareState(PartitionAwareState *state);
static inline void ThorwLimitExceededError(int32 nDocumentsGenerated);

static uint32 DensifyFullKeyHashFunc(const void *obj, size_t objsize);
static pgbson * DensifyPartitionCore(PG_FUNCTION_ARGS, DensifyType type);
static void TimeStepIncrementor(bson_value_t *baseValue, TypedStep *step);
static void NumericStepIncrementor(bson_value_t *baseValue, TypedStep *step);
static int DensifyFullKeyHashCompare(const void *obj1, const void *obj2, Size objsize);
static void PopulateDensifyArgs(DensifyArguments *arguments, const pgbson *densifySpec);
static bool IsPartitionByFieldsOnShardKey(const pgbson *partitionByFields,
										  const MongoCollection *collection);
static void CreateProjectionTreeStateForPartitionBy(DensifyWindowState *state,
													pgbson *partitionByFields);
static PartitionAwareState * GetValidPartitionAwareState(DensifyWindowState *state,
														 pgbson *document);
static void InitializeDensifyWindowState(DensifyWindowState *state, const
										 pgbson *densifySpec,
										 DensifyType type);
static bool CheckEnoughRoomForNewDocuments(const bson_value_t *min, const
										   bson_value_t *max,
										   const DensifyWindowState *state);

static void GetDensifyQueryExprs(DensifyArguments *arguments,
								 AggregationPipelineBuildContext *context,
								 Expr *docExpr, bool isBaseDataTable,
								 Expr **partitionByFieldsExpr,
								 SortBy **sortByExpr, Oid *densifyFunctionOid);
static pgbson * DensifyDocsFromState(const pgbson *document, bson_value_t *minValue,
									 bson_value_t *maxValue,
									 const pgbson *partitioBy, DensifyWindowState *state);
static bson_value_t GenerateAndWriteDocumentsInRange(const bson_value_t *minValue, const
													 bson_value_t *maxValue,
													 const pgbson *partitioBy,
													 DensifyWindowState *state,
													 pgbson_array_writer *arrayWriter,
													 bool includeMaxBound);
static Query * GenerateSelectNullQuery(void);
static Query * GenerateUnionAllWithSelectNullQuery(Query *baseQuery,
												   AggregationPipelineBuildContext *
												   context);

/* Window functions to support different modes of densify */

PG_FUNCTION_INFO_V1(bson_densify_range);
PG_FUNCTION_INFO_V1(bson_densify_partition);
PG_FUNCTION_INFO_V1(bson_densify_full);
PG_FUNCTION_INFO_V1(densify_partition_by_fields);

/*
 * densify_partition_by_fields recieves the document and the composite path fields
 * that define the unique grouping for densify stage.
 */
Datum
densify_partition_by_fields(PG_FUNCTION_ARGS)
{
	pgbson *partitionByFields = PG_GETARG_MAYBE_NULL_PGBSON_PACKED(1);
	if (partitionByFields == NULL)
	{
		PG_RETURN_NULL();
	}

	int argPositions[1] = { 1 };
	int numArgs = 1;

	DensifyWindowState *state = NULL;
	SetCachedFunctionStateMultiArgs(
		state,
		DensifyWindowState,
		argPositions,
		numArgs,
		CreateProjectionTreeStateForPartitionBy,
		partitionByFields);

	pgbson *document = PG_GETARG_MAYBE_NULL_PGBSON_PACKED(0);
	if (document == NULL)
	{
		PG_RETURN_NULL();
	}

	if (state == NULL)
	{
		state = palloc0(sizeof(DensifyWindowState));
		CreateProjectionTreeStateForPartitionBy(state, partitionByFields);
	}

	bool moveToExecutorContext = false;
	pgbson *result = GetPartitionByFieldsPgbson(document, state, moveToExecutorContext);

	PG_FREE_IF_COPY(document, 0);
	PG_FREE_IF_COPY(partitionByFields, 1);

	if (result == NULL)
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_POINTER(result);
}


/* bson_densify_range
 *    Fills the documents within the user supplied lower and upper bound range.
 * Lower bound is inclusive and upper bound is exclusive.
 *
 * document => The document that needs to be densified
 * densifySpec => $densify spec of this form:
 *     {
 *         'partitionByFields': [<fields>],
 *          field: <field>,
 *          range: {
 *              bounds: [<lower>, <upper>],
 *              steps: <number>,
 *              unit: <timeunit>
 *          }
 *     }
 *
 */
Datum
bson_densify_range(PG_FUNCTION_ARGS)
{
	pgbson *result = DensifyPartitionCore(fcinfo, DENSIFY_TYPE_RANGE);
	PG_RETURN_POINTER(result);
}


/* bson_densify_partition
 *    Fills the documents within the partition's lower and upper bounds.
 *
 * document => The document that needs to be densified
 * densifySpec => $densify spec of this form:
 *     {
 *         'partitionByFields': [<fields>],
 *          field: <field>,
 *          range: {
 *              bounds: "partition",
 *              steps: <number>,
 *              unit: <timeunit>
 *          }
 *     }
 *
 */
Datum
bson_densify_partition(PG_FUNCTION_ARGS)
{
	pgbson *result = DensifyPartitionCore(fcinfo, DENSIFY_TYPE_PARTITION);
	PG_RETURN_POINTER(result);
}


/* bson_densify_full
 *    Fills the documents for a range that is minimum and maximum across
 *    all the partitions.
 *
 * document => The document that needs to be densified
 * densifySpec => $densify spec of this form:
 *     {
 *         'partitionByFields': [<fields>],
 *          field: <field>,
 *          range: {
 *              bounds: "full",
 *              steps: <number>,
 *              unit: <timeunit>
 *          }
 *     }
 *
 * This window function is not expected to be called for a window query with PARTITION BY
 * specified. It internally maintains a hash table of groups and keeps track of densifaction
 * across the overall range of partitions.
 * The documents are processed in an ORDER BY for the `field` value
 */
Datum
bson_densify_full(PG_FUNCTION_ARGS)
{
	WindowObject winObject = PG_WINDOW_OBJECT();

	bool isNull = false;
	pgbson *document = DatumGetPgBson(WinGetFuncArgCurrent(winObject, 0, &isNull));
	pgbson *spec = DatumGetPgBson(WinGetFuncArgCurrent(winObject, 1, &isNull));

	DensifyWindowState *densifyState = NULL;
	int argPosition = 1;
	SetCachedFunctionState(
		densifyState,
		DensifyWindowState,
		argPosition,
		InitializeDensifyWindowState,
		spec,
		DENSIFY_TYPE_FULL);

	if (densifyState == NULL)
	{
		densifyState = palloc0(sizeof(DensifyWindowState));
		InitializeDensifyWindowState(densifyState, spec, DENSIFY_TYPE_FULL);
	}

	PartitionAwareState *partitionState = GetValidPartitionAwareState(densifyState,
																	  document);

	/* Find the group entry in hash, if this a new group create the entry for this group */
	HTAB *partitionGroupTable = partitionState->partitionInfo;
	bool found;
	DensifyFullEntry searchPartitionKey = { 0 };
	bool moveToExecutorContext = false;
	searchPartitionKey.evaluatedPartitionBy = GetPartitionByFieldsPgbson(document,
																		 densifyState,
																		 moveToExecutorContext);
	DensifyFullEntry *foundEntry = hash_search(partitionGroupTable, &searchPartitionKey,
											   HASH_ENTER, &found);
	if (!found)
	{
		/* When a new group is seen, copy the group by value in executor context so that
		 * it doesn't vanish in the hash table, and set the lastMinValueInGroup to the first
		 * row's value so that densification starts from global minimum value
		 */
		if (searchPartitionKey.evaluatedPartitionBy != NULL)
		{
			foundEntry->evaluatedPartitionBy =
				CopyPgbsonIntoMemoryContext(searchPartitionKey.evaluatedPartitionBy,
											densifyState->executorContext);
		}

		/*
		 * Note: arguments.lowerBound is not available in case of `full` but we use it to store
		 * the minimum value which is the first row's value and because there is no default partitioning done
		 * for `full` it is also the minimum across all partitions that we will create inside this function
		 * using hash
		 */
		foundEntry->lastMinValueInGroup = densifyState->arguments.lowerBound;
	}

	/* Check if this is the last row in the partition */
	bool isOut = false;
	isNull = false;
	WinGetFuncArgInPartition(winObject, 1, 1, WINDOW_SEEK_CURRENT, false,
							 &isNull, &isOut);
	partitionState->isLastRow = isOut;

	pgbson *densifiedDocs = DensifyDocsFromState(document,
												 &foundEntry->lastMinValueInGroup,
												 &densifyState->arguments.upperBound,
												 foundEntry->evaluatedPartitionBy,
												 densifyState);

	/* Update the lastMinValue of the group */
	foundEntry->lastMinValueInGroup = partitionState->lastMinValue;

	WinSetMarkPosition(winObject, WinGetCurrentPosition(winObject));

	if (partitionState->isLastRow)
	{
		/*
		 * Since there is single partition this means we are done
		 * free all the held resources.
		 */
		ReleasePartitionAwareState(&densifyState->partitionAwareState);
		PgbsonFreeIfNotNull(densifyState->arguments.partitionByFields);
		if (densifyState->partitionByTreeState != NULL)
		{
			pfree(densifyState->partitionByTreeState);
		}
		pfree(densifyState);
	}
	PG_RETURN_POINTER(densifiedDocs);
}


/*
 * Aggregation pipleine stage handler for `$densify`.
 * Converts to a window function which densifies the incoming documents based on the mode inline
 * which are later unwinded using bson_lookup_unwind.
 *
 * The query returned is of the form:
 * if densify_spec is:
 *     {"partitionByFields": ["a"], "field": "b", "range": {"bounds": [10, 15], "step": 1 }}
 * SELECT bson_lookup_unwind(stage2.document, '_') FROM (
 *     SELECT bson_densify_range(stage1.document, <densify_spec>) OVER (PARTITION BY densify_partition_by_fields(stage1.document, '{"": ["a"]}')) as document
 *     FROM (
 *          ... Query from previous aggregation pipelines
 *     ) stage1
 * ) as stage2
 *
 * The window functions used for different modes are:
 * {"bounds": [10, 15]} => bson_densify_range
 * {"bounds": "partition"} => bson_densify_partition
 * {"bounds": "full"} => bson_densify_full
 *
 * If the aggregation query is on an empty_data_table and the mode is `range`
 * then we need to generate at least documents in the range even when there are no documents in the table
 * for this we use a non window function `bson_densify_range_emptytable`
 */
Query *
HandleDensify(const bson_value_t *existingValue, Query *query,
			  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_DENSIFY);

	if (!EnableDensifyStage || !IsClusterVersionAtleastThis(1, 22, 0))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg("$densify aggregation stage is not supported yet.")));
	}

	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"The $densify stage specification must be an object, found %s",
							BsonTypeName(existingValue->value_type)),
						errdetail_log(
							"The $densify stage specification must be an object, found %s",
							BsonTypeName(existingValue->value_type))));
	}

	RangeTblEntry *rte = linitial(query->rtable);

	bool isMongoDataTable = rte->rtekind == RTE_RELATION;

	pgbson *densifySpec = PgbsonInitFromDocumentBsonValue(existingValue);
	Const *densifySpecConst = MakeBsonConst(densifySpec);

	DensifyArguments arguments;
	memset(&arguments, 0, sizeof(DensifyArguments));
	PopulateDensifyArgs(&arguments, densifySpec);

	/*
	 * Special case for range mode densify.
	 * Mongo generates documents in the range even when the collection is
	 * - Non existing.
	 * - Empty collection.
	 * - Filtered documents are 0.
	 *
	 * Convert base query to ... UNION ALL SELECT NULL, to add a NULL row
	 * at the end which will help generate the docs in the range.
	 *
	 * This is done due to the fact that projectors are not called
	 * if there are no rows.
	 */
	if (arguments.densifyType == DENSIFY_TYPE_RANGE)
	{
		query = GenerateUnionAllWithSelectNullQuery(query, context);

		/* We create union all query now its not a data table alone anymore */
		isMongoDataTable = false;
	}

	if (!isMongoDataTable)
	{
		query = MigrateQueryToSubQuery(query, context);
	}

	TargetEntry *firstEntry = linitial(query->targetList);
	Expr *docExpr = (Expr *) firstEntry->expr;

	Expr *partitionByFieldsExpr = NULL;
	SortBy *sortByExpr = NULL;
	Oid densifyFunctionOid = InvalidOid;
	GetDensifyQueryExprs(&arguments, context, docExpr, isMongoDataTable,
						 &partitionByFieldsExpr, &sortByExpr, &densifyFunctionOid);

	/* Add the window function for densify */
	WindowFunc *densifyWindowFunc = makeNode(WindowFunc);
	densifyWindowFunc->winfnoid = densifyFunctionOid;
	densifyWindowFunc->winagg = false;
	densifyWindowFunc->winstar = false;
	densifyWindowFunc->winref = 1;
	densifyWindowFunc->wintype = BsonTypeId();
	densifyWindowFunc->args = list_make2(docExpr, densifySpecConst);

	firstEntry->expr = (Expr *) densifyWindowFunc;

	WindowClause *winClause = makeNode(WindowClause);
	winClause->winref = 1;

	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_WINDOW_PARTITION;
	parseState->p_next_resno = list_length(query->targetList) + 1;

	bool resjunk = true;

	if (partitionByFieldsExpr != NULL)
	{
		/* Add parition by expr for the window clause if not null */
		TargetEntry *partitionTle = makeTargetEntry((Expr *) partitionByFieldsExpr,
													parseState->p_next_resno++,
													"?densifyPartitionBy?", resjunk);
		assignSortGroupRef(partitionTle, query->targetList);
		query->targetList = lappend(query->targetList, partitionTle);

		SortGroupClause *partitionClause = makeNode(SortGroupClause);
		partitionClause->tleSortGroupRef = partitionTle->ressortgroupref;
		partitionClause->eqop = BsonEqualOperatorId();
		partitionClause->sortop = BsonLessThanOperatorId();
		partitionClause->nulls_first = false;
		partitionClause->hashable = true;

		winClause->partitionClause = list_make1(partitionClause);
	}

	TargetEntry *sortEntry = makeTargetEntry((Expr *) sortByExpr->node,
											 parseState->p_next_resno++,
											 NULL, resjunk);
	query->targetList = lappend(query->targetList, sortEntry);
	List *sortClauseList = NIL;
	sortClauseList = addTargetToSortList(parseState, sortEntry, sortClauseList,
										 query->targetList, sortByExpr);
	winClause->orderClause = sortClauseList;
	query->hasWindowFuncs = true;
	query->windowClause = list_make1(winClause);

	/* Move everything to subquery so that we can apply bson_lookup_unwind */
	query = MigrateQueryToSubQuery(query, context);

	firstEntry = linitial(query->targetList);
	List *unwindArgs = list_make2(firstEntry->expr, MakeTextConst(DENSIFY_RESULT_FIELD,
																  DENSIFY_RESULT_FIELD_LENGTH));
	FuncExpr *lookupUnwindExpr = makeFuncExpr(BsonLookupUnwindFunctionOid(), BsonTypeId(),
											  unwindArgs, InvalidOid, InvalidOid,
											  COERCE_EXPLICIT_CALL);
	lookupUnwindExpr->funcretset = true;
	firstEntry->expr = (Expr *) lookupUnwindExpr;
	query->hasTargetSRFs = true;

	context->requiresSubQuery = true;
	return query;
}


/*======================*/
/* Private functions    */
/*======================*/

/*
 * Returns the values found in document by the treeState created by `partitiotnByFields` value
 * as a document.
 * Path that are not present are not included with any default values and are skipped.
 *  document = {"a": 1, "b": 2, "c": 3}
 *  partitionByFields = {"": ["a", "c", "d"]}
 *
 * returns  {"a": 1, "c": 3}
 */
static inline pgbson *
GetPartitionByFieldsPgbson(pgbson *document, DensifyWindowState *state, bool
						   moveToExecutorContext)
{
	if (document == NULL || state == NULL || state->partitionByTreeState == NULL)
	{
		return NULL;
	}

	pgbson *partitionByField = ProjectDocumentWithState(document,
														state->partitionByTreeState);

	if (moveToExecutorContext && state->executorContext != NULL &&
		state->executorContext != CurrentMemoryContext)
	{
		partitionByField = CopyPgbsonIntoMemoryContext(partitionByField,
													   state->executorContext);
	}

	return partitionByField;
}


/*
 * Writes the partitionBy bson into the writer for the newly generated doc.
 */
static inline void
AddGroupByValueToGeneratedDocuments(pgbson_writer *writer, const pgbson *partitionBy)
{
	if (writer == NULL || partitionBy == NULL)
	{
		return;
	}
	bson_iter_t partitionByItr;
	PgbsonInitIterator(partitionBy, &partitionByItr);
	while (bson_iter_next(&partitionByItr))
	{
		PgbsonWriterAppendIter(writer, &partitionByItr);
	}
}


/*
 * Validates if there are the fieldValue is of correct type required for $densify.
 * Null and EOD values are allowed.
 * Numbers are allowed only when the `unit` is not provided.
 * Dates are allowed only when the `unit` is provided.
 * Any other type is not allowed.
 */
static inline void
CheckFieldValue(const bson_value_t *fieldValue, DensifyWindowState *state)
{
	if (fieldValue->value_type == BSON_TYPE_NULL || fieldValue->value_type ==
		BSON_TYPE_EOD)
	{
		return;
	}

	bool isDateUnitPresent = state->arguments.timeUnit != DateUnit_Invalid;

	if (!BsonValueIsNumber(fieldValue) && fieldValue->value_type != BSON_TYPE_DATE_TIME)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733201),
						errmsg("PlanExecutor error during aggregation :: caused by :: "
							   "Densify field type must be numeric or a date")));
	}

	if (isDateUnitPresent && BsonValueIsNumber(fieldValue))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION6053600),
						errmsg("PlanExecutor error during aggregation :: caused by :: "
							   "Encountered numeric densify value in collection when step has a date unit.")));
	}

	if (!isDateUnitPresent && fieldValue->value_type == BSON_TYPE_DATE_TIME)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION6053600),
						errmsg("PlanExecutor error during aggregation :: caused by :: "
							   "Encountered date densify value in collection when step does not have a date unit.")));
	}
}


/*
 * Frees the partitionAwareState
 */
static inline void
ReleasePartitionAwareState(PartitionAwareState *partitionState)
{
	if (partitionState == NULL)
	{
		return;
	}

	/* Clear the previous partition state if any here */
	PgbsonFreeIfNotNull(partitionState->evaluatedPartitionBy);
	if (partitionState->partitionInfo != NULL)
	{
		hash_destroy(partitionState->partitionInfo);
	}
	memset(partitionState, 0, sizeof(PartitionAwareState));
}


static inline void
pg_attribute_noreturn()
ThorwLimitExceededError(int32 nDocumentsGenerated)
{
	ereport(ERROR, (
				errcode(ERRCODE_HELIO_LOCATION5897900),
				errmsg(
					"PlanExecutor error during aggregation :: caused by :: Generated %d documents in $densify"
					", which is over the limit of %d. Increase the 'internalQueryMaxAllowedDensifyDocs' parameter"
					" to allow more generated documents",
					nDocumentsGenerated, PEC_InternalQueryMaxAllowedDensifyDocs),
				errdetail_log(
					"Generated %d documents in $densify, which is over the limit of %d.",
					nDocumentsGenerated, PEC_InternalQueryMaxAllowedDensifyDocs)));
}

static inline void
pg_attribute_noreturn()
ThrowMemoryLimitExceededError(int meConsumed)
{
	ereport(ERROR, (
				errcode(ERRCODE_HELIO_LOCATION6007200),
				errmsg(
					"PlanExecutor error during aggregation :: caused by :: "
					"$densify exceeded memory limit of %d",
					PEC_InternalDocumentSourceDensifyMaxMemoryBytes),
				errdetail_log(
					"$densify exceeded memory limit of %d",
					PEC_InternalDocumentSourceDensifyMaxMemoryBytes)));
}


/*
 * Core implementation of generating documents for densify. Currently this supports both
 * `range` and `partition` mode of densify.
 */
static pgbson *
DensifyPartitionCore(PG_FUNCTION_ARGS, DensifyType type)
{
	WindowObject winObject = PG_WINDOW_OBJECT();

	bool isNull = false;
	pgbson *spec = DatumGetPgBson(WinGetFuncArgCurrent(winObject, 1, &isNull));

	DensifyWindowState *densifyState = NULL;
	int argPosition = 1;
	SetCachedFunctionState(
		densifyState,
		DensifyWindowState,
		argPosition,
		InitializeDensifyWindowState,
		spec,
		type);

	if (densifyState == NULL)
	{
		densifyState = palloc0(sizeof(DensifyWindowState));
		InitializeDensifyWindowState(densifyState, spec, type);
	}

	pgbson *document = NULL;
	isNull = false;
	Datum documentDatum = WinGetFuncArgCurrent(winObject, 0, &isNull);
	if (isNull)
	{
		if (type == DENSIFY_TYPE_RANGE &&
			densifyState->previousType == DENSIFY_DOCUMENT_VALID)
		{
			/*
			 * Ignore this row because NULL row is added via a UNION
			 * query to help generate documents in case of:
			 *     - Non existing collection.
			 *     - Empty collection.
			 *     - Filtered collection with 0 rows.
			 */
			PG_RETURN_NULL();
		}
	}
	else
	{
		document = DatumGetPgBson(documentDatum);
	}

	densifyState->previousType = isNull ? DENSIFY_DOCUMENT_NULL : DENSIFY_DOCUMENT_VALID;

	PartitionAwareState *partitionState = GetValidPartitionAwareState(densifyState,
																	  document);

	/* Check if this is the last row in the partition */
	bool isOut = false;
	isNull = false;
	WinGetFuncArgInPartition(winObject, 1, 1, WINDOW_SEEK_CURRENT, false,
							 &isNull, &isOut);
	partitionState->isLastRow = isOut;

	pgbson *densifiedDocs = DensifyDocsFromState(document,
												 &partitionState->lastMinValue,
												 &densifyState->arguments.upperBound,
												 partitionState->evaluatedPartitionBy,
												 densifyState);
	WinSetMarkPosition(winObject, WinGetCurrentPosition(winObject));

	/* Reset the partition state in the next call if this is last row of the current partition */
	partitionState->reset = partitionState->isLastRow;
	return densifiedDocs;
}


/*
 * Validates the densify spec and gets the `partitionByFieldExpr`
 * `sortByExpr` and `densifyFunctionOid` to use while building query
 * tree
 */
static void
GetDensifyQueryExprs(DensifyArguments *arguments,
					 AggregationPipelineBuildContext *context,
					 Expr *docExpr, bool isDataTable, Expr **partitionByFieldsExpr,
					 SortBy **sortByExpr, Oid *densifyFunctionOid)
{
	if (arguments->densifyType == DENSIFY_TYPE_RANGE)
	{
		*densifyFunctionOid = BsonDensifyRangeWindowFunctionOid();
	}
	else if (arguments->densifyType == DENSIFY_TYPE_PARTITION)
	{
		*densifyFunctionOid = BsonDensifyPartitionWindowFunctionOid();
	}
	else if (arguments->densifyType == DENSIFY_TYPE_FULL)
	{
		*densifyFunctionOid = BsonDensifyFullWindowFunctionOid();
	}

	if (arguments->densifyType != DENSIFY_TYPE_FULL && arguments->partitionByFields !=
		NULL)
	{
		/*
		 * For `full` mode there is no partition created and every row is processed
		 * in a single partition in the ascending order of `field`. The partitioning
		 * metadata is stored in `bson_densify_full` function to generate right set
		 * of documents
		 */

		if (isDataTable && IsPartitionByFieldsOnShardKey(arguments->partitionByFields,
														 context->mongoCollection))
		{
			*partitionByFieldsExpr = (Expr *) makeVar(((Var *) docExpr)->varno,
													  MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
													  INT8OID, -1,
													  InvalidOid, 0);
		}
		else
		{
			Const *partitionConst = MakeBsonConst(arguments->partitionByFields);
			*partitionByFieldsExpr = (Expr *) makeFuncExpr(
				BsonDensifyPartitionFunctionId(),
				BsonTypeId(), list_make2(
					docExpr, partitionConst),
				InvalidOid, InvalidOid,
				COERCE_EXPLICIT_CALL);
		}
	}

	pgbson_writer sortSpecWriter;
	PgbsonWriterInit(&sortSpecWriter);
	PgbsonWriterAppendInt64(&sortSpecWriter, arguments->field.string,
							arguments->field.length, 1);
	pgbson *sortSpec = PgbsonWriterGetPgbson(&sortSpecWriter);

	Expr *expr = (Expr *) makeFuncExpr(BsonOrderByFunctionOid(), BsonTypeId(),
									   list_make2(docExpr, MakeBsonConst(sortSpec)),
									   InvalidOid, InvalidOid,
									   COERCE_EXPLICIT_CALL);
	SortBy *sortBy = makeNode(SortBy);
	sortBy->location = -1;
	sortBy->node = (Node *) expr;
	sortBy->sortby_nulls = SORTBY_NULLS_FIRST;
	sortBy->sortby_dir = SORTBY_ASC;

	*sortByExpr = sortBy;
}


/*
 * DensifyDocsFromState returns an array pgbson with the current document and all other
 * documents added to densify the list
 * e.g.
 * document = {b: 10, a: 1}
 * step = { "": 2 }
 * minValue = {"": 6}
 * maxValue = {"": 14}
 * then this function would return:
 * if document is the last row in the group then
 * {"_": [ {b: 6}, {b: 8}, {b: 10, a: 1}, {b: 12}, {b: 14} ]}
 *
 * otherwise densifies till the current value
 * {"_": [ {b: 6}, {b: 8}, {b: 10, a: 1} ]}
 *
 * For `partition` and `full` mode the maxValue is equal to the current value in
 * document so it always densifies upto the current value
 *
 * If the time unit is present the returned array is date values.
 */
static pgbson *
DensifyDocsFromState(const pgbson *document, bson_value_t *minValue,
					 bson_value_t *maxValue,
					 const pgbson *partitionBy, DensifyWindowState *state)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, DENSIFY_RESULT_FIELD,
						   DENSIFY_RESULT_FIELD_LENGTH, &arrayWriter);

	bool includeMaxBound = false;
	bson_value_t lastMinUpdatedValue = { 0 };
	bson_value_t currentFieldValue = { 0 };

	DensifyArguments *arguments = &state->arguments;
	PartitionAwareState *partitionState = &state->partitionAwareState;

	if (!partitionState->skipRows)
	{
		if (document != NULL)
		{
			PgbsonGetBsonValueAtPath(document, arguments->field.string,
									 &currentFieldValue);
			CheckFieldValue(&currentFieldValue, state);
		}

		bool overflowedFromInt = false;
		bool isComparisionValid = true;
		int upperBoundCompareResult = maxValue->value_type == BSON_TYPE_EOD ? -1 :
									  CompareBsonValueAndType(&currentFieldValue,
															  maxValue,
															  &overflowedFromInt);

		if (minValue->value_type == BSON_TYPE_EOD ||
			minValue->value_type == BSON_TYPE_NULL)
		{
			/*
			 * Generally EOD and NULL values are placed before regular values in an ASC sort
			 * so whenever the minValue is any of these we can make the min=currentFieldValue
			 * and write the document.
			 * By doing this we progress till we start finding regular value where densification
			 * can be done
			 */
			*minValue = currentFieldValue;
		}

		int compareResult = CompareBsonValueAndType(&currentFieldValue, minValue,
													&isComparisionValid);

		if (upperBoundCompareResult > 0)
		{
			/* Doc is out of upper bound add all pending step values and mark the state to skip rows for next set of documents */
			partitionState->skipRows = true;
			lastMinUpdatedValue = GenerateAndWriteDocumentsInRange(minValue, maxValue,
																   partitionBy, state,
																   &arrayWriter,
																   includeMaxBound);
			partitionState->lastMinValue = lastMinUpdatedValue;
		}
		else if (compareResult > 0)
		{
			/* The current value is greater than last generated min value and is not out of bounds, densify till currentValue */
			lastMinUpdatedValue = GenerateAndWriteDocumentsInRange(minValue,
																   &currentFieldValue,
																   partitionBy, state,
																   &arrayWriter,
																   includeMaxBound);
			partitionState->lastMinValue = lastMinUpdatedValue;
		}
		else if (compareResult == 0)
		{
			/*
			 * Update the state's lastMinValue to the next available value after the current value
			 * and don't write any documents for this row
			 * Next lastMinValue = currentFieldValue + step
			 */
			lastMinUpdatedValue = currentFieldValue;
			state->incrementor(&lastMinUpdatedValue, &state->typedStep);
			partitionState->lastMinValue = lastMinUpdatedValue;
		}
	}

	if (document != NULL)
	{
		PgbsonArrayWriterWriteDocument(&arrayWriter, document);
	}

	if (partitionState->isLastRow && state->arguments.densifyType == DENSIFY_TYPE_RANGE)
	{
		/* If this is last row and we have not exhausted the range try generating documents for the remaning range */
		GenerateAndWriteDocumentsInRange(&partitionState->lastMinValue,
										 &state->arguments.upperBound,
										 partitionBy, state, &arrayWriter,
										 includeMaxBound);
	}

	if (partitionState->isLastRow && state->arguments.densifyType == DENSIFY_TYPE_FULL)
	{
		/* Get all partitions and try to add documents for each group till new minValue */
		HASH_SEQ_STATUS status;
		DensifyFullEntry *entry;
		HTAB *partitionInfo = partitionState->partitionInfo;
		hash_seq_init(&status, partitionInfo);

		/* We need to include the max bounds for other groups as the maxBound is the current field value of
		 * the group we are processing
		 */
		includeMaxBound = true;
		while ((entry = (DensifyFullEntry *) hash_seq_search(&status)) != NULL)
		{
			if (PgbsonEquals(partitionBy, entry->evaluatedPartitionBy))
			{
				/* This is the current group we are processing and it is already densified
				 * just update the lastMinValueInGroup and move to the new group.
				 */
				continue;
			}

			GenerateAndWriteDocumentsInRange(&entry->lastMinValueInGroup,
											 &currentFieldValue,
											 entry->evaluatedPartitionBy, state,
											 &arrayWriter,
											 includeMaxBound);
		}
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Parses the densifySpec and populates the values in DensifyArguments structure
 */
static void
PopulateDensifyArgs(DensifyArguments *arguments, const pgbson *densifySpec)
{
	Assert(arguments != NULL);
	bson_iter_t iter;
	PgbsonInitIterator(densifySpec, &iter);
	bson_value_t fieldValue = { 0 };
	bson_value_t rangeValue = { 0 };
	bson_value_t partitionByFieldsValue = { 0 };

	while (bson_iter_next(&iter))
	{
		pgbsonelement element;
		BsonIterToPgbsonElement(&iter, &element);

		if (strcmp(element.path, "field") == 0)
		{
			fieldValue = element.bsonValue;
		}
		else if (strcmp(element.path, "range") == 0)
		{
			rangeValue = element.bsonValue;
		}
		else if (strcmp(element.path, "partitionByFields") == 0)
		{
			partitionByFieldsValue = element.bsonValue;
		}
	}

	/* Parse required field */
	if (fieldValue.value_type != BSON_TYPE_EOD)
	{
		EnsureTopLevelFieldValueType("$densify.field", &fieldValue, BSON_TYPE_UTF8);
		EnsureStringValueNotDollarPrefixed(fieldValue.value.v_utf8.str,
										   fieldValue.value.v_utf8.len);

		arguments->field = CreateStringViewFromString(fieldValue.value.v_utf8.str);
	}
	else
	{
		ThrowTopLevelMissingFieldErrorWithCode("$densify.field",
											   ERRCODE_HELIO_LOCATION40414);
	}

	/* Parse required range */
	if (rangeValue.value_type != BSON_TYPE_EOD)
	{
		EnsureTopLevelFieldValueType("$densify.range", &rangeValue, BSON_TYPE_DOCUMENT);

		bson_iter_t rangeIter;
		BsonValueInitIterator(&rangeValue, &rangeIter);
		while (bson_iter_next(&rangeIter))
		{
			pgbsonelement rangeElement;
			BsonIterToPgbsonElement(&rangeIter, &rangeElement);

			if (strcmp(rangeElement.path, "bounds") == 0)
			{
				if (rangeElement.bsonValue.value_type != BSON_TYPE_ARRAY &&
					rangeElement.bsonValue.value_type != BSON_TYPE_UTF8)
				{
					ereport(ERROR, (
								errcode(ERRCODE_HELIO_LOCATION5733402),
								errmsg(
									"the bounds in a range statement must be the string 'full', 'partition',"
									" or an ascending array of two numbers or two dates")));
				}

				if (rangeElement.bsonValue.value_type == BSON_TYPE_UTF8)
				{
					if (strcmp(rangeElement.bsonValue.value.v_utf8.str,
							   "partition") == 0)
					{
						arguments->densifyType = DENSIFY_TYPE_PARTITION;
					}
					else if (strcmp(rangeElement.bsonValue.value.v_utf8.str,
									"full") == 0)
					{
						arguments->densifyType = DENSIFY_TYPE_FULL;
					}
					else
					{
						ereport(ERROR, (
									errcode(ERRCODE_HELIO_LOCATION5946802),
									errmsg(
										"Bounds string must either be 'full' or 'partition'")));
					}
				}
				else if (rangeElement.bsonValue.value_type == BSON_TYPE_ARRAY)
				{
					bson_iter_t boundsIter;
					BsonValueInitIterator(&rangeElement.bsonValue, &boundsIter);
					int index = 0;
					while (bson_iter_next(&boundsIter))
					{
						pgbsonelement boundElement;
						BsonIterToPgbsonElement(&boundsIter, &boundElement);
						if (index == 0)
						{
							arguments->lowerBound = boundElement.bsonValue;
						}
						else if (index == 1)
						{
							arguments->upperBound = boundElement.bsonValue;
						}
						index++;
					}

					if (index != 2)
					{
						ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733403),
										errmsg(
											"a bounding array in a range statement must have exactly two elements")));
					}

					arguments->densifyType = DENSIFY_TYPE_RANGE;
				}
			}
			else if (strcmp(rangeElement.path, "step") == 0)
			{
				EnsureTopLevelFieldIsNumberLike("$densify.range.step",
												&rangeElement.bsonValue);
				if (IsBsonValueNegativeNumber(&rangeElement.bsonValue) ||
					BsonValueAsDouble(&rangeElement.bsonValue) == 0.0)
				{
					ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733401),
									errmsg(
										"the step parameter in a range statement must be a strictly positive numeric value")));
				}
				arguments->step = rangeElement.bsonValue;
			}
			else if (strcmp(rangeElement.path, "unit") == 0)
			{
				EnsureTopLevelFieldValueType("$densify.range.unit",
											 &rangeElement.bsonValue, BSON_TYPE_UTF8);
				arguments->timeUnit = GetDateUnitFromString(
					rangeElement.bsonValue.value.v_utf8.str);

				if (arguments->timeUnit == DateUnit_Invalid)
				{
					ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
									errmsg("unknown time unit value: %s",
										   rangeElement.bsonValue.value.v_utf8.str)));
				}
			}
		}
	}
	else
	{
		ThrowTopLevelMissingFieldErrorWithCode("$densify.range",
											   ERRCODE_HELIO_LOCATION40414);
	}

	/* Parse the option partitionByFields */
	if (partitionByFieldsValue.value_type != BSON_TYPE_EOD)
	{
		EnsureTopLevelFieldValueType("$densify.partitionByFields",
									 &partitionByFieldsValue, BSON_TYPE_ARRAY);

		bson_iter_t childIter;
		BsonValueInitIterator(&partitionByFieldsValue, &childIter);
		while (bson_iter_next(&childIter))
		{
			const bson_value_t *path = bson_iter_value(&childIter);

			if (path->value_type != BSON_TYPE_UTF8)
			{
				/* palloc string into only for error case */
				StringInfo index = makeStringInfo();
				appendStringInfo(index, "$densify.partitionByFields.%s",
								 bson_iter_key(&childIter));
				EnsureTopLevelFieldValueType(index->data, path, BSON_TYPE_UTF8);
			}

			EnsureStringValueNotDollarPrefixed(path->value.v_utf8.str,
											   path->value.v_utf8.len);

			if (StringViewEqualsCString(&arguments->field, path->value.v_utf8.str))
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_LOCATION8993000),
							errmsg(
								"BSON field '$densify.partitionByFields' contains the field that is being densified")));
			}
		}

		if (!IsBsonValueEmptyArray(&partitionByFieldsValue))
		{
			arguments->partitionByFields = BsonValueToDocumentPgbson(
				&partitionByFieldsValue);
		}
	}

	/* Validation for missing fields or invalid combinations */
	if (arguments->densifyType == DENSIFY_TYPE_INVALID)
	{
		/* No bounds provided */
		ThrowTopLevelMissingFieldErrorWithCode("$densify.range.bounds",
											   ERRCODE_HELIO_LOCATION40414);
	}

	if (arguments->step.value_type == BSON_TYPE_EOD)
	{
		ThrowTopLevelMissingFieldErrorWithCode("$densify.range.step",
											   ERRCODE_HELIO_LOCATION40414);
	}

	if (arguments->partitionByFields == NULL && arguments->densifyType ==
		DENSIFY_TYPE_PARTITION)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733408),
						errmsg(
							"one may not specify the bounds as 'partition' without specifying "
							"a non-empty array of partitionByFields. You may have meant to specify 'full' bounds.")));
	}

	if (arguments->densifyType == DENSIFY_TYPE_RANGE)
	{
		bool isComparisionValid = false;
		if (CompareBsonValueAndType(&arguments->lowerBound, &arguments->upperBound,
									&isComparisionValid) > 0)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733402),
							errmsg(
								"the bounds in a range statement must be the string 'full', 'partition',"
								" or an ascending array of two numbers or two dates")));
		}
		else if (BsonTypeIsNumber(arguments->lowerBound.value_type) &&
				 arguments->timeUnit != DateUnit_Invalid)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733409),
							errmsg("numeric bounds may not have unit parameter")));
		}
		else if (BsonValueIsNumber(&arguments->lowerBound) &&
				 arguments->upperBound.value_type == BSON_TYPE_DATE_TIME)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733406),
							errmsg(
								"a bounding array must contain either both dates or both numeric types")));
		}
		else if (BsonValueIsNumber(&arguments->upperBound) &&
				 arguments->lowerBound.value_type == BSON_TYPE_DATE_TIME)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5733402),
							errmsg(
								"a bounding array must be an ascending array of either two dates or two numbers")));
		}
		else if (arguments->lowerBound.value_type == BSON_TYPE_DATE_TIME &&
				 !IsBsonValueFixedInteger(&arguments->step))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION6586400),
							errmsg(
								"The step parameter in a range satement must be a whole number when densifying a date range")));
		}
		else if (arguments->timeUnit == DateUnit_Invalid &&
				 !(arguments->step.value_type == arguments->lowerBound.value_type &&
				   arguments->step.value_type == arguments->upperBound.value_type))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5876900),
							errmsg(
								"Upper bound, lower bound, and step must all have the same type")));
		}
	}
}


/*
 * GenerateAndWriteDocumentsInRange generates documents between min and max values
 * increment by the step value.
 * minValue is inclusive and
 * maxValue is always exclusive unless includeMaxBound is true.
 *
 * Returns the last exclusive value till where densification is done excluding this
 * value
 *
 * Throws error if number of generated doc till now exceeds the max number of allowed documents
 * to be generated for densify
 */
static bson_value_t
GenerateAndWriteDocumentsInRange(const bson_value_t *minValue, const
								 bson_value_t *maxValue,
								 const pgbson *partitionBy, DensifyWindowState *state,
								 pgbson_array_writer *arrayWriter,
								 bool includeMaxBound)
{
	bool isComparisionValid = true;
	bson_value_t generatedValue = *minValue;

	if (!CheckEnoughRoomForNewDocuments(minValue, maxValue, state))
	{
		ThorwLimitExceededError(PEC_InternalQueryMaxAllowedDensifyDocs + 1);
	}

	int compareResult = CompareBsonValueAndType(&generatedValue, maxValue,
												&isComparisionValid);
	while (compareResult < 0 || (includeMaxBound && compareResult == 0))
	{
		CHECK_FOR_INTERRUPTS();
		check_stack_depth();

		pgbson_writer childWriter;
		PgbsonArrayWriterStartDocument(arrayWriter, &childWriter);
		AddGroupByValueToGeneratedDocuments(&childWriter,
											partitionBy);
		PgbsonWriterAppendValue(&childWriter,
								state->arguments.field.string,
								state->arguments.field.length,
								&generatedValue);

		if (compareResult != 0)
		{
			/* Exclude existing docs */
			state->nDocumentsGenerated++;
			state->memConsumed += PgbsonWriterGetSize(&childWriter);
		}

		PgbsonArrayWriterEndDocument(arrayWriter, &childWriter);

		if (state->nDocumentsGenerated > PEC_InternalQueryMaxAllowedDensifyDocs)
		{
			ThorwLimitExceededError(state->nDocumentsGenerated);
		}

		if (state->memConsumed > PEC_InternalDocumentSourceDensifyMaxMemoryBytes)
		{
			ThrowMemoryLimitExceededError(state->memConsumed);
		}

		/* Increment to next value */
		state->incrementor(&generatedValue, &state->typedStep);
		compareResult = CompareBsonValueAndType(&generatedValue, maxValue,
												&isComparisionValid);
	}

	/* Update the new value in state */
	if (compareResult == 0)
	{
		/*
		 * Mongodb retains the last seen value and maintains its type.
		 * Copy the original value of max as this is matched completely
		 * and then add the step to maintain type
		 */
		generatedValue = *maxValue;
		state->incrementor(&generatedValue, &state->typedStep);
	}
	return generatedValue;
}


static void
NumericStepIncrementor(bson_value_t *baseValue, TypedStep *step)
{
	if (baseValue->value_type == BSON_TYPE_EOD || baseValue->value_type == BSON_TYPE_NULL)
	{
		return;
	}

	if (!BsonValueIsNumber(baseValue))
	{
		/*TODO: Error*/
	}
	bool overflowFromInt = false;
	AddNumberToBsonValue(baseValue, &step->numericStep, &overflowFromInt);
}


static void
TimeStepIncrementor(bson_value_t *baseValue, TypedStep *step)
{
	if (baseValue->value_type == BSON_TYPE_EOD || baseValue->value_type == BSON_TYPE_NULL)
	{
		return;
	}

	if (baseValue->value_type != BSON_TYPE_DATE_TIME)
	{
		/*TODO: ERROR*/
	}
	Datum pgTimeStamp = GetPgTimestampFromUnixEpoch(baseValue->value.v_datetime);
	Datum result = DirectFunctionCall2(timestamp_pl_interval, pgTimeStamp,
									   step->intervalStep);
	float8 resultSeconds = DatumGetFloat8(DirectFunctionCall2(
											  timestamp_part,
											  CStringGetTextDatum(EPOCH),
											  result));
	baseValue->value.v_datetime = (int64_t) (resultSeconds * MILLISECONDS_IN_SECOND);
}


static uint32
DensifyFullKeyHashFunc(const void *obj, size_t objsize)
{
	DensifyFullEntry *entry = (DensifyFullEntry *) obj;

	if (entry->evaluatedPartitionBy == NULL)
	{
		return 0;
	}

	bson_iter_t partitionByIter;
	PgbsonInitIterator(entry->evaluatedPartitionBy, &partitionByIter);
	int64 seed = 0;
	int64 hash = BsonHashCompare(&partitionByIter, HashBytesUint32AsUint64,
								 HashCombineUint32AsUint64, seed);

	return (uint32) hash;
}


static int
DensifyFullKeyHashCompare(const void *obj1, const void *obj2, Size objsize)
{
	const DensifyFullEntry *entry1 = (const DensifyFullEntry *) obj1;
	const DensifyFullEntry *entry2 = (const DensifyFullEntry *) obj2;

	if (entry1->evaluatedPartitionBy == NULL && entry2->evaluatedPartitionBy == NULL)
	{
		return 0;
	}

	if (entry1->evaluatedPartitionBy == NULL)
	{
		return -1;
	}

	if (entry2->evaluatedPartitionBy == NULL)
	{
		return 1;
	}

	return ComparePgbson(entry1->evaluatedPartitionBy, entry2->evaluatedPartitionBy);
}


/*
 * Checks if partitionByFields expression of $densify stage is on the shard key
 * of the collection
 *
 * `partitionByFields`: { "": ["a", "b", "c"]}
 * `shardkey`: {"a": "hashed", "b": "hashed", "c": "hashed"}
 *
 * These 2 are same
 */
static bool
IsPartitionByFieldsOnShardKey(const pgbson *partitionByFields, const
							  MongoCollection *collection)
{
	if (collection == NULL || collection->shardKey == NULL || partitionByFields == NULL)
	{
		return false;
	}

	pgbson_writer shardKeyWriter;
	pgbson_array_writer arrayWriter;
	PgbsonWriterInit(&shardKeyWriter);
	PgbsonWriterStartArray(&shardKeyWriter, "", 0, &arrayWriter);

	bson_iter_t shardKeyIter;
	PgbsonInitIterator(collection->shardKey, &shardKeyIter);
	while (bson_iter_next(&shardKeyIter))
	{
		PgbsonArrayWriterWriteUtf8(&arrayWriter, bson_iter_key(&shardKeyIter));
	}
	PgbsonWriterEndArray(&shardKeyWriter, &arrayWriter);

	if (PgbsonEquals(PgbsonWriterGetPgbson(&shardKeyWriter), partitionByFields))
	{
		return true;
	}

	return false;
}


/* Converts the {"": ["a", "b", "c"]} into a projection spec
 * of this form {"a": 1, "b": 1, "c": 1, "_id": 0}, so that the projection tree
 * can be created and cached as per requirement
 */
static void
CreateProjectionTreeStateForPartitionBy(DensifyWindowState *state, pgbson *partitionBy)
{
	if (partitionBy == NULL)
	{
		return;
	}

	pgbsonelement elem;
	PgbsonToSinglePgbsonElement(partitionBy, &elem);

	if (elem.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		return;
	}

	pgbson_writer projectionSpecWriter;
	PgbsonWriterInit(&projectionSpecWriter);
	bson_iter_t partitionValueItr;
	BsonValueInitIterator(&elem.bsonValue, &partitionValueItr);

	while (bson_iter_next(&partitionValueItr))
	{
		const bson_value_t *pathValue = bson_iter_value(&partitionValueItr);
		PgbsonWriterAppendInt32(&projectionSpecWriter, pathValue->value.v_utf8.str,
								pathValue->value.v_utf8.len, 1);
	}

	/* Exclude _id if any */
	PgbsonWriterAppendInt32(&projectionSpecWriter, "_id", 3, 0);

	pgbson *densifyPartitionProjectionSpec = PgbsonWriterGetPgbson(&projectionSpecWriter);
	bson_iter_t iter;
	PgbsonInitIterator(densifyPartitionProjectionSpec, &iter);

	bool forceProjectId = false;
	bool allowInclusionExclusion = true;
	state->partitionByTreeState =
		(BsonProjectionQueryState *) GetProjectionStateForBsonProject(&iter,
																	  forceProjectId,
																	  allowInclusionExclusion);
}


static PartitionAwareState *
GetValidPartitionAwareState(DensifyWindowState *state, pgbson *document)
{
	PartitionAwareState *partitionState = &state->partitionAwareState;
	if (partitionState->reset)
	{
		ReleasePartitionAwareState(partitionState);
		memset(partitionState, 0, sizeof(PartitionAwareState));
	}

	if (partitionState->isInitialized)
	{
		/* Already initialized, nothing to do */
		return partitionState;
	}

	memset(partitionState, 0, sizeof(PartitionAwareState));
	partitionState->isInitialized = true;

	bool moveToExecutorContext = true;
	partitionState->evaluatedPartitionBy = GetPartitionByFieldsPgbson(document, state,
																	  moveToExecutorContext);

	DensifyArguments *args = &state->arguments;
	if (args->densifyType == DENSIFY_TYPE_RANGE)
	{
		/*
		 * For `range` the lowerBound value is initialized to be the lastMinValue
		 */
		partitionState->lastMinValue = args->lowerBound;
	}
	else
	{
		bson_value_t currentFieldValue = { 0 };
		PgbsonGetBsonValueAtPath(document, args->field.string, &currentFieldValue);
		CheckFieldValue(&currentFieldValue, state);

		/*
		 * We can generally advance the lastMinValue to next value after first rows value,
		 * as the first row would be already written because it is document from the collection,
		 * and densification is done on the next value.
		 */
		partitionState->lastMinValue = currentFieldValue;
		state->incrementor(&partitionState->lastMinValue, &state->typedStep);

		if (args->densifyType == DENSIFY_TYPE_FULL)
		{
			/* Starting of the range is always the minmum value across all partitions
			 * which is the value of the first row, so we use args->lowerBound as a placeholder
			 * to store the minimum value across all partitions
			 * Any new group that needs to start densification will use this lowest value as the starting point
			 */
			args->lowerBound = currentFieldValue;

			HASHCTL hashinfo = CreateExtensionHashCTL(
				sizeof(DensifyFullEntry),
				sizeof(DensifyFullEntry),
				DensifyFullKeyHashCompare,
				DensifyFullKeyHashFunc
				);

			/* Create the hash that tracks densification for each group in
			 * the longer lived memory context
			 */
			hashinfo.hcxt = state->executorContext;
			partitionState->partitionInfo = hash_create("Densify full partition hash", 32,
														&hashinfo,
														DefaultExtensionHashFlags);

			/*
			 * Create the hash entry for the group of the very first row
			 */
			DensifyFullEntry firstGroupEntryKey = { 0 };
			firstGroupEntryKey.evaluatedPartitionBy =
				partitionState->evaluatedPartitionBy;

			bool found = false;
			DensifyFullEntry *entry = hash_search(partitionState->partitionInfo,
												  &firstGroupEntryKey, HASH_ENTER,
												  &found);
			entry->evaluatedPartitionBy = firstGroupEntryKey.evaluatedPartitionBy;
			entry->lastMinValueInGroup = partitionState->lastMinValue;
		}
	}
	return partitionState;
}


/*
 * Initializes a new DensifyWindowState with the given densifySpec and type
 */
static void
InitializeDensifyWindowState(DensifyWindowState *state, const pgbson *densifySpec,
							 DensifyType type)
{
	MemoryContext ctxt = CurrentMemoryContext;

	state->executorContext = ctxt;
	PopulateDensifyArgs(&state->arguments, densifySpec);
	DensifyArguments *args = &state->arguments;

	Assert(args->densifyType == type);

	/* Create and intialize projection tree */
	CreateProjectionTreeStateForPartitionBy(state, args->partitionByFields);

	/* Set the step incrementors */
	if (args->timeUnit != DateUnit_Invalid)
	{
		int64 amount = BsonValueAsInt64(&args->step);
		state->typedStep.intervalStep = GetIntervalFromDateUnitAndAmount(args->timeUnit,
																		 amount);
		state->incrementor = &TimeStepIncrementor;
	}
	else
	{
		state->typedStep.numericStep = args->step;
		state->incrementor = &NumericStepIncrementor;
	}
	state->previousType = DENSIFY_DOCUMENT_UNKNOWN;
	memset(&state->partitionAwareState, 0, sizeof(PartitionAwareState));
}


/*
 * Checks if there is enough room before even trying to generate the documents
 */
static bool
CheckEnoughRoomForNewDocuments(const bson_value_t *min, const bson_value_t *max,
							   const DensifyWindowState *state)
{
	int32 nDocsAvailable = PEC_InternalQueryMaxAllowedDensifyDocs -
						   state->nDocumentsGenerated;
	if (state->arguments.timeUnit != DateUnit_Invalid)
	{
		int64 minMillis = min->value.v_datetime;
		int64 maxMillis = max->value.v_datetime;
		float8 secondsInInterval = DatumGetFloat8(DirectFunctionCall2(
													  interval_part,
													  CStringGetTextDatum(EPOCH),
													  state->typedStep.intervalStep));
		double nDocsToGenerate = (maxMillis - minMillis) / (secondsInInterval *
															MILLISECONDS_IN_SECOND);
		return nDocsToGenerate <= (double) nDocsAvailable;
	}
	else
	{
		bool overflowedFromInt = false;
		bson_value_t result = *max;
		SubtractNumberFromBsonValue(&result, min, &overflowedFromInt);
		DivideBsonValueNumbers(&result, &state->arguments.step);

		bson_value_t nDocsAvailableValue = {
			.value.v_int32 = nDocsAvailable,
			.value_type = BSON_TYPE_INT32
		};

		return CompareBsonValueAndType(&result, &nDocsAvailableValue,
									   &overflowedFromInt) <= 0;
	}

	return false;
}


/*
 * Generates a SELECT NULL query;
 */
static Query *
GenerateSelectNullQuery()
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	query->rtable = NIL;

	/* Create an empty jointree */
	query->jointree = makeNode(FromExpr);

	/* Create the projector for NULL::bson */
	Const *nullConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, (Datum) 0, true,
								 false);
	TargetEntry *baseNullEntry = makeTargetEntry((Expr *) nullConst, 1, "document",
												 false);
	query->targetList = list_make1(baseNullEntry);
	return query;
}


/*
 * Generates a SELECT document from (baseQuery UNION ALL SELECT NULL) query
 */
static Query *
GenerateUnionAllWithSelectNullQuery(Query *baseQuery,
									AggregationPipelineBuildContext *context)
{
	Query *leftQuery = baseQuery;
	Query *rightQuery = GenerateSelectNullQuery();

	bool includeAllColumns = false;
	RangeTblEntry *leftRte = MakeSubQueryRte(leftQuery, context->stageNum, 0,
											 "densifyRangeLeft",
											 includeAllColumns);
	RangeTblEntry *rightRte = MakeSubQueryRte(rightQuery, context->stageNum, 0,
											  "densifyRangeRight",
											  includeAllColumns);

	Query *unionRangeQuery = makeNode(Query);
	unionRangeQuery->commandType = CMD_SELECT;
	unionRangeQuery->querySource = baseQuery->querySource;
	unionRangeQuery->canSetTag = true;
	unionRangeQuery->jointree = makeNode(FromExpr);

	unionRangeQuery->rtable = list_make2(leftRte, rightRte);
	RangeTblRef *leftReference = makeNode(RangeTblRef);
	leftReference->rtindex = 1;
	RangeTblRef *rightReference = makeNode(RangeTblRef);
	rightReference->rtindex = 2;

	SetOperationStmt *setOpStatement = MakeBsonSetOpStatement();
	setOpStatement->larg = (Node *) leftReference;
	setOpStatement->rarg = (Node *) rightReference;

	/* Update the query with UNION ALL SetOp statement */
	unionRangeQuery->setOperations = (Node *) setOpStatement;

	/* Result column node */
	TargetEntry *leftMostTargetEntry = linitial(leftQuery->targetList);
	Var *var = makeVar(1, leftMostTargetEntry->resno,
					   BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *restle = makeTargetEntry((Expr *) var,
										  leftMostTargetEntry->resno,
										  leftMostTargetEntry->resname,
										  false);
	unionRangeQuery->targetList = list_make1(restle);
	return unionRangeQuery;
}
