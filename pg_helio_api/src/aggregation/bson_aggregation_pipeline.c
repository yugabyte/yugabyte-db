/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/bson_aggregation_pipeline.c
 *
 * Implementation of the backend query generation for pipelines.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <optimizer/optimizer.h>

#include <access/table.h>
#include <access/reloptions.h>
#include <utils/rel.h>
#include <catalog/namespace.h>
#include <optimizer/planner.h>
#include <nodes/nodes.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <parser/parser.h>
#include <parser/parse_agg.h>
#include <parser/parse_clause.h>
#include <parser/parse_param.h>
#include <parser/analyze.h>
#include <parser/parse_oper.h>
#include <utils/ruleutils.h>
#include <utils/builtins.h>
#include <catalog/pg_aggregate.h>
#include <catalog/pg_operator.h>
#include <catalog/pg_class.h>
#include <parser/parsetree.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/numeric.h>
#include <nodes/supportnodes.h>
#include <parser/parse_relation.h>

#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "planner/helio_planner.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "commands/defrem.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "customscan/helio_custom_scan.h"

#include "aggregation/bson_aggregation_pipeline_private.h"
#include "vector/vector_common.h"
#include "aggregation/bson_project.h"
#include "operators/bson_expression_bucket_operator.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "utils/version_utils.h"

/*
 * The mutation function that modifies a given query with a pipeline stage's value.
 */
typedef Query *(*MutateQueryForStageFunc)(const bson_value_t *existingValue,
										  Query *query,
										  AggregationPipelineBuildContext *context);

/*
 * Whether or not the stage requires persistence on the cursor.
 */
typedef bool (*RequiresPersistentCursorFunc)(const bson_value_t *existingValue);

/*
 * Whether or not the stage can be inlined for a lookup stage
 */
typedef bool (*CanInlineLookupStage)(const bson_value_t *stageValue, const
									 StringView *lookupPath);


/*
 * Declaration of a given aggregation pipeline stage.
 */
typedef struct
{
	/* The stage name in Mongo format (e.g. $addFields, $project) */
	const char *stage;

	/* The function that will modify the pipeline for that stage - NULL if unsupported */
	MutateQueryForStageFunc mutateFunc;

	/* Whether or not the stage requires persistence on cursors */
	RequiresPersistentCursorFunc requiresPersistentCursor;

	/* Optional function for whether lookup stages can be inlined in the substage */
	CanInlineLookupStage canInlineLookupStageFunc;

	/* Does the stage maintain a stable sort order or modify the input stream */
	bool preservesStableSortOrder;

	/* Whether or not the stage supports collection agnostic queries
	 */
	bool canHandleAgnosticQueries;

	/* Whether or not the stage is a linear transform of the prior stage.
	 * i.e. whether or not the stage is purely a projection transform of its input.
	 */
	bool isProjectTransform;

	/* Whether or not the stage is an output stage. $merge and $out are output stages */
	bool isOutputStage;
} AggregationStageDefinition;


static void TryHandleSimplifyAggregationRequest(SupportRequestSimplify *simplifyRequest);
static void AddCursorFunctionsToQuery(Query *query, Query *baseQuery,
									  QueryData *queryData,
									  AggregationPipelineBuildContext *context,
									  bool addCursorAsConst);
static void SetBatchSize(const char *fieldName, const bson_value_t *value,
						 QueryData *queryData);

static int CompareStageByStageName(const void *a, const void *b);
static bool IsDefaultJoinTree(Node *node);
static List * AddShardKeyAndIdFilters(const bson_value_t *existingValue, Query *query,
									  AggregationPipelineBuildContext *context,
									  TargetEntry *entry, List *existingQuals);

/* Stage functions */
static Query * HandleAddFields(const bson_value_t *existingValue, Query *query,
							   AggregationPipelineBuildContext *context);
static Query * HandleBucket(const bson_value_t *existingValue, Query *query,
							AggregationPipelineBuildContext *context);
static Query * HandleCount(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);
static Query * HandleLimit(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);
static Query * HandleProject(const bson_value_t *existingValue, Query *query,
							 AggregationPipelineBuildContext *context);
static Query * HandleProjectFind(const bson_value_t *existingValue,
								 const bson_value_t *queryValue, Query *query,
								 AggregationPipelineBuildContext *context);
static Query * HandleReplaceRoot(const bson_value_t *existingValue, Query *query,
								 AggregationPipelineBuildContext *context);
static Query * HandleReplaceWith(const bson_value_t *existingValue, Query *query,
								 AggregationPipelineBuildContext *context);
static Query * HandleSample(const bson_value_t *existingValue, Query *query,
							AggregationPipelineBuildContext *context);
static Query * HandleSkip(const bson_value_t *existingValue, Query *query,
						  AggregationPipelineBuildContext *context);
static Query * HandleSort(const bson_value_t *existingValue, Query *query,
						  AggregationPipelineBuildContext *context);
static Query * HandleSortByCount(const bson_value_t *existingValue, Query *query,
								 AggregationPipelineBuildContext *context);
static Query * HandleUnset(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);
static Query * HandleUnwind(const bson_value_t *existingValue, Query *query,
							AggregationPipelineBuildContext *context);
static Query * HandleDistinct(const StringView *existingValue, Query *query,
							  AggregationPipelineBuildContext *context);
static Query * HandleGeoNear(const bson_value_t *existingValue, Query *query,
							 AggregationPipelineBuildContext *context);
static Query * HandleMatchAggregationStage(const bson_value_t *existingValue,
										   Query *query,
										   AggregationPipelineBuildContext *context);

static bool RequiresPersistentCursorFalse(const bson_value_t *pipelineValue);
static bool RequiresPersistentCursorTrue(const bson_value_t *pipelineValue);
static bool RequiresPersistentCursorLimit(const bson_value_t *pipelineValue);
static bool RequiresPersistentCursorSkip(const bson_value_t *pipelineValue);

static bool CanInlineLookupStageProjection(const bson_value_t *stageValue, const
										   StringView *lookupPath);
static bool CanInlineLookupStageUnset(const bson_value_t *stageValue, const
									  StringView *lookupPath);
static bool CanInlineLookupStageUnwind(const bson_value_t *stageValue, const
									   StringView *lookupPath);
static bool CanInlineLookupStageTrue(const bson_value_t *stageValue, const
									 StringView *lookupPath);

extern bool EnableGroupMergeObjectsSupport;
extern bool EnableCursorsOnAggregationQueryRewrite;

static bool CheckFuncExprBsonDollarProjectGeonear(const FuncExpr *funcExpr);

static void ValidateQueryTreeForMatchStage(const Query *query);

/* Stages and their definitions sorted by name.
 * Please keep this list sorted.
 */
static const AggregationStageDefinition StageDefinitions[] =
{
	{
		.stage = "$_internalInhibitOptimization",
		.mutateFunc = &HandleInternalInhibitOptimization,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageTrue,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$addFields",
		.mutateFunc = &HandleAddFields,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = &CanInlineLookupStageProjection,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$bucket",
		.mutateFunc = &HandleBucket,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$bucketAuto",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$changeStream",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = true,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$collStats",
		.mutateFunc = &HandleCollStats,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$count",
		.mutateFunc = &HandleCount,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* Changes the projector - can't be inlined */
		.canInlineLookupStageFunc = NULL,

		/* Count changes the output format */
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$currentOp",
		.mutateFunc = &HandleCurrentOp,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* Changes the projector - can't be inlined */
		.canInlineLookupStageFunc = NULL,

		/* currentOp changes the output format */
		.preservesStableSortOrder = false,

		.canHandleAgnosticQueries = true,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$densify",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$documents",
		.mutateFunc = &HandleDocumentsStage,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = true,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$facet",
		.mutateFunc = &HandleFacet,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* Changes the projector - can't be inlined */
		.canInlineLookupStageFunc = NULL,

		/* Count changes the output format (object_agg) so no prior sorts */
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$fill",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$geoNear",
		.mutateFunc = &HandleGeoNear,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$graphLookup",
		.mutateFunc = &HandleGraphLookup,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$group",
		.mutateFunc = &HandleGroup,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* Changes the projector - can't be inlined */
		.canInlineLookupStageFunc = NULL,

		/* Count changes the output format (group) so no prior sorts
		 * are valid after this*/
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$indexStats",
		.mutateFunc = &HandleIndexStats,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$inverseMatch",
		.mutateFunc = &HandleInverseMatch,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageTrue,

		/* inverse match does not change the output format */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$limit",
		.mutateFunc = &HandleLimit,
		.requiresPersistentCursor = &RequiresPersistentCursorLimit,

		/* Cannot be inlined - needs to happen *after* the join */
		.canInlineLookupStageFunc = NULL,

		/* Limit does not change the output format */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$listLocalSessions",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = true,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$listSessions",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$lookup",
		.mutateFunc = &HandleLookup,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = &CanInlineLookupStageLookup,

		/* Lookup preserves order of the Left collection */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$match",
		.mutateFunc = &HandleMatchAggregationStage,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageTrue,

		/* Match does not change the output format */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$merge",
		.mutateFunc = &HandleMerge,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = true,
	},
	{
		.stage = "$out",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = true,
	},
	{
		.stage = "$project",
		.mutateFunc = &HandleProject,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = &CanInlineLookupStageProjection,

		/* Project does not change the output format */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$redact",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$replaceRoot",
		.mutateFunc = &HandleReplaceRoot,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,

		/* Changes document structure - can never be inlined */
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$replaceWith",
		.mutateFunc = &HandleReplaceWith,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,

		/* Changes document structure - can never be inlined */
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$sample",
		.mutateFunc = &HandleSample,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* Needs to be applied post-join - should not be inlined */
		.canInlineLookupStageFunc = NULL,

		/* Sample is effectively a Random orderby - Prior sorts are invalid */
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$search",
		.mutateFunc = &HandleSearch,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageTrue,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$searchMeta",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$set",
		.mutateFunc = &HandleAddFields,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = &CanInlineLookupStageProjection,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$setWindowFields",
		.mutateFunc = NULL,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$skip",
		.mutateFunc = &HandleSkip,
		.requiresPersistentCursor = &RequiresPersistentCursorSkip,

		/* Cannot be inlined - needs to happen *after* the join */
		.canInlineLookupStageFunc = NULL,

		/* Skip does not change the output format */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$sort",
		.mutateFunc = &HandleSort,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageTrue,

		/* Sort sets the ordering state */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$sortByCount",
		.mutateFunc = &HandleSortByCount,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$unionWith",
		.mutateFunc = &HandleUnionWith,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$unset",
		.mutateFunc = &HandleUnset,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = &CanInlineLookupStageUnset,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
	},
	{
		.stage = "$unwind",
		.mutateFunc = &HandleUnwind,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = &CanInlineLookupStageUnwind,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	},
	{
		.stage = "$vectorSearch",
		.mutateFunc = &HandleMongoNativeVectorSearch,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageTrue,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
	}
};

static const int AggregationStageCount = sizeof(StageDefinitions) /
										 sizeof(AggregationStageDefinition);

PG_FUNCTION_INFO_V1(command_bson_aggregation_pipeline);
PG_FUNCTION_INFO_V1(command_api_collection);
PG_FUNCTION_INFO_V1(command_aggregation_support);


inline static void
EnsureTopLevelNumberFieldType(const char *fieldName, const bson_value_t *value)
{
	if (!BsonValueIsNumber(value))
	{
		ThrowTopLevelTypeMismatchError(fieldName, BsonTypeName(
										   value->value_type), "number");
	}
}


/*
 * Creates a Var in the query representing the 'document' column of a helio_data table.
 */
inline static Var *
CreateDocumentVar(void)
{
	/* the only Var in the Query context (if any) */
	Index varno = 1;

	/* not lives in a subquery */
	Index varlevelsup = 0;
	return makeVar(varno, MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER,
				   BsonTypeId(), MONGO_DATA_TABLE_DOCUMENT_VAR_TYPMOD,
				   MONGO_DATA_TABLE_DOCUMENT_VAR_COLLATION, varlevelsup);
}


/*
 * command_bson_aggregation_pipeline is a wrapper function that carries with it the
 * aggregation pipeline for a query. This is replaced in the planner and so shouldn't
 * ever be called in the runtime.
 */
Datum
command_bson_aggregation_pipeline(PG_FUNCTION_ARGS)
{
	/* dumbest possible implementation: assume 1% of rows are returned */
	ereport(ERROR, (errmsg(
						"bson_aggregation function should have been processed by the planner. This is an internal error")));
	PG_RETURN_BOOL(false);
}


/*
 * The core Collection function. Represents a query:
 * e.g. SELECT * from ApiSchema.collection().
 */
Datum
command_api_collection(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errmsg(
						"Collection function should have been simplified during planning. "
						"Collection function should be only used in a FROM clause")));
}


/*
 * This is a support function for any aggregation functions
 * - aggregation pipeline
 * - find
 * - count
 * - distinct
 * These during the planning phase are replaced by the actual SQL query
 * that is run during the execution phase.
 */
Datum
command_aggregation_support(PG_FUNCTION_ARGS)
{
	Node *supportRequest = (Node *) PG_GETARG_POINTER(0);
	if (IsA(supportRequest, SupportRequestSimplify))
	{
		/* Try to convert operator/function call to index conditions */
		SupportRequestSimplify *req =
			(SupportRequestSimplify *) supportRequest;

		TryHandleSimplifyAggregationRequest(req);
	}

	PG_RETURN_POINTER(NIL);
}


/*
 * Traverses the query looking for an aggregation pipeline function.
 * If it's found, then replaces the function with nothing, and updates the query
 * to track the contents of the aggregation pipeline.
 */
Query *
ExpandAggregationFunction(Query *query, ParamListInfo boundParams)
{
	/* Top level validations - these are right now during development */

	/* Today we only support a query of the form
	 * SELECT document FROM bson_aggregation_pipeline('db', 'pipeline bson'::bson);
	 * OR
	 * SELECT document FROM bson_aggregation_find('db', 'findSpec bson'::bson);
	 * This restriction exists during the development phase and can change as we move to prod.
	 */
	if (list_length(query->rtable) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have 1 collection. Found %d. This is unexpected",
							list_length(query->rtable))));
	}

	if (query->jointree == NULL)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have at least 1 collection and query. This is unexpected")));
	}

	if (list_length(query->jointree->fromlist) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have exactly 1 collection to query from not %d. This is unexpected",
							list_length(query->jointree->fromlist))));
	}

	if (list_length(query->cteList) > 0)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have CTEs. This is currently unsupported")));
	}

	if (list_length(query->targetList) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline query should not have more than 1 projector Found %d. This is currently unsupported",
							list_length(query->targetList))));
	}

	if (query->limitOffset != NULL || query->limitCount != NULL)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline query should not have skip/limit. This is currently unsupported")));
	}

	if (query->sortClause != NIL)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline query should not have sort. This is currently unsupported")));
	}

	TargetEntry *targetEntry = linitial(query->targetList);
	if (!IsA(targetEntry->expr, Var))
	{
		ereport(ERROR, (errmsg(
							"Projector must be a single (alias-ed) column. This is unexpected")));
	}

	if (query->jointree->quals != NULL)
	{
		ereport(ERROR, (errmsg(
							"Query must not have filters. This is unexpected")));
	}

	RangeTblEntry *rte = (RangeTblEntry *) linitial(query->rtable);

	if (rte->rtekind != RTE_FUNCTION || list_length(rte->functions) != 1)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should select from the Mongo aggregation function kind %d. This is unexpected",
							rte->rtekind)));
	}

	RangeTblFunction *rangeTblFunc = linitial(rte->functions);
	if (!IsA(rangeTblFunc->funcexpr, FuncExpr))
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should select is not a function. This is unexpected")));
	}

	FuncExpr *aggregationFunc = (FuncExpr *) rangeTblFunc->funcexpr;

	if (list_length(aggregationFunc->args) != 2)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline node should have 2 args. This is unexpected")));
	}

	Node *databaseArg = linitial(aggregationFunc->args);
	Node *secondArg = lsecond(aggregationFunc->args);

	if (!IsA(secondArg, Const) || !IsA(databaseArg, Const))
	{
		secondArg = EvaluateBoundParameters(secondArg, boundParams);
		databaseArg = EvaluateBoundParameters(databaseArg, boundParams);
	}

	if (!IsA(secondArg, Const) || !IsA(databaseArg, Const))
	{
		/* Let the runtime deal with this (This will either go to the runtime function and fail, or noop due to prepared and come back here
		 * to be evaluated during the EXECUTE)
		 */
		return query;
	}

	Const *databaseConst = (Const *) databaseArg;
	Const *aggregationConst = (Const *) secondArg;
	if (databaseConst->constisnull || aggregationConst->constisnull)
	{
		ereport(ERROR, (errmsg(
							"Aggregation pipeline arguments should not be null. This is unexpected")));
	}

	pgbson *pipeline = DatumGetPgBson(aggregationConst->constvalue);

	QueryData queryData = { 0 };
	bool enableCursorParam = false;
	if (aggregationFunc->funcid == ApiCatalogAggregationPipelineFunctionId())
	{
		return GenerateAggregationQuery(databaseConst->constvalue, pipeline, &queryData,
										enableCursorParam);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationFindFunctionId())
	{
		return GenerateFindQuery(databaseConst->constvalue, pipeline, &queryData,
								 enableCursorParam);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationCountFunctionId())
	{
		return GenerateCountQuery(databaseConst->constvalue, pipeline);
	}
	else if (aggregationFunc->funcid == ApiCatalogAggregationDistinctFunctionId())
	{
		return GenerateDistinctQuery(databaseConst->constvalue, pipeline);
	}
	else
	{
		ereport(ERROR, (errmsg(
							"Unrecognized pipeline functionid provided. This is unexpected")));
	}
}


/*
 * Common utility function for parsing a batchSize argument for find/aggregate/getMore
 */
void
SetBatchSize(const char *fieldName, const bson_value_t *value, QueryData *queryData)
{
	EnsureTopLevelNumberFieldType(fieldName, value);

	/* Batchsize can be double - as long as it converts to int (which it treats as floor) */
	queryData->batchSize = BsonValueAsInt32(value);

	if (queryData->batchSize < 0)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("BatchSize value must be non-negative, but received: %d",
							   queryData->batchSize)));
	}
}


/*
 * Common utility function for parsing a the namespace (Collection) argument for find/aggregate
 * Updates the cursor state with the namespaceName 'db.coll'
 */
const char *
CreateNamespaceName(text *databaseName, const StringView *collectionName)
{
	char *databaseString = VARDATA_ANY(databaseName);
	uint32_t databaseLength = VARSIZE_ANY_EXHDR(databaseName);

	/* Format for database.collection */
	uint32_t totalLength = databaseLength + 1 + collectionName->length + 1;

	char *finalString = palloc(totalLength);
	memcpy(finalString, databaseString, databaseLength);
	finalString[databaseLength] = '.';
	memcpy(&finalString[databaseLength + 1], collectionName->string,
		   collectionName->length);
	finalString[totalLength - 1] = 0;

	return finalString;
}


/*
 * Validates the sanity of an aggregation pipeline.
 * For this we simply create the query from it and let the validation take place.
 */
void
ValidateAggregationPipeline(Datum databaseDatum, const StringView *baseCollection,
							const bson_value_t *pipelineValue)
{
	AggregationPipelineBuildContext validationContext = { 0 };
	validationContext.databaseNameDatum = databaseDatum;

	pg_uuid_t *collectionUuid = NULL;
	Query *validationQuery = GenerateBaseTableQuery(databaseDatum,
													baseCollection,
													collectionUuid,
													&validationContext);
	validationQuery = MutateQueryWithPipeline(validationQuery, pipelineValue,
											  &validationContext);
	pfree(validationQuery);
}


/*
 * Core logic that takes a pipeline array specified by pipelineValue,
 * and updates the "query" with the pipeline stages' effects.
 * The mutated query is returned as an output.
 */
Query *
MutateQueryWithPipeline(Query *query, const bson_value_t *pipelineValue,
						AggregationPipelineBuildContext *context)
{
	bson_iter_t pipelineIterator;
	BsonValueInitIterator(pipelineValue, &pipelineIterator);

	/* $merge and $out are output stage must be last stage of pipeline */
	const char *lastEncounteredOutputStage = NULL;

	while (bson_iter_next(&pipelineIterator))
	{
		bson_iter_t documentIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIterator) ||
			!bson_iter_recurse(&pipelineIterator, &documentIterator))
		{
			ereport(ERROR, (errcode(MongoTypeMismatch),
							errmsg(
								"Each element of the 'pipeline' array must be an object")));
		}

		pgbsonelement stageElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&documentIterator, &stageElement))
		{
			ereport(ERROR, (errcode(MongoLocation40323),
							errmsg(
								"A pipeline stage specification object must contain exactly one field.")));
		}

		/* If lastEncounteredOutputStage isn't NULL, it means we've seen an output stage like $out or $merge before this.
		 * Since the output stage was expected to be last, encountering it earlier leads to failure. */
		if (lastEncounteredOutputStage != NULL)
		{
			ereport(ERROR, (errcode(MongoLocation40601),
							errmsg("%s can only be the final stage in the pipeline",
								   lastEncounteredOutputStage),
							errhint("%s can only be the final stage in the pipeline",
									lastEncounteredOutputStage)));
		}

		/* Now handle each stage */
		AggregationStageDefinition *definition = (AggregationStageDefinition *) bsearch(
			stageElement.path, StageDefinitions,
			AggregationStageCount,
			sizeof(
				AggregationStageDefinition),
			CompareStageByStageName);
		if (definition == NULL)
		{
			ereport(ERROR, (errcode(MongoUnrecognizedCommand),
							errmsg("Unrecognized pipeline stage name: %s",
								   stageElement.path),
							errhint("Unrecognized pipeline stage name: %s",
									stageElement.path)));
		}

		if (definition->mutateFunc == NULL)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg(
								"Stage %s is not supported yet in native pipeline",
								definition->stage),
							errhint("Stage %s is not supported yet in native pipeline",
									definition->stage)));
		}

		if (definition->isOutputStage)
		{
			lastEncounteredOutputStage = definition->stage;
		}

		/* If we're an agnostic query (we're not selecting from anything),
		 * ensure that the stage handles agnostic-ness in the query */
		if (query->jointree->fromlist == NIL && !definition->canHandleAgnosticQueries)
		{
			ereport(ERROR, (errcode(MongoInvalidNamespace),
							errmsg(
								"{aggregate: 1} is not valid for '%s'; a collection is required.",
								stageElement.path),
							errhint(
								"{aggregate: 1} is not valid for '%s'; a collection is required.",
								stageElement.path)));
		}

		/* If the prior stage needed to be pushed to a sub-query before the next
		 * stage is processed, then add a subquery stage
		 */
		if (context->requiresSubQuery)
		{
			query = MigrateQueryToSubQuery(query, context);
		}

		if (context->requiresSubQueryAfterProject)
		{
			context->requiresSubQueryAfterProject = false;
			if (definition->isProjectTransform)
			{
				/* Push to subquery on the next stage */
				context->requiresSubQuery = true;
			}
			else
			{
				/* Not a project, so push to a subquery */
				query = MigrateQueryToSubQuery(query, context);
			}
		}

		query = definition->mutateFunc(&stageElement.bsonValue, query,
									   context);
		context->requiresPersistentCursor =
			context->requiresPersistentCursor ||
			definition->requiresPersistentCursor(&stageElement.bsonValue);

		if (!definition->preservesStableSortOrder)
		{
			context->sortSpec.value_type = BSON_TYPE_EOD;
		}
		context->stageNum++;
	}

	/* Agnostic query with no stages */
	if (context->stageNum == 0 && query->jointree->fromlist == NULL)
	{
		/* make the query Select NULL::bson LIMIT 0 */
		query->limitCount = (Node *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
											   Int64GetDatum(0), false, true);
	}

	return query;
}


/*
 * Given a query and an aggregation pipeline mutates the query
 * to match the contents of the provided aggregation pipeline.
 */
Query *
GenerateAggregationQuery(Datum database, pgbson *aggregationSpec, QueryData *queryData,
						 bool addCursorParams)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = database;

	bson_iter_t aggregationIterator;
	PgbsonInitIterator(aggregationSpec, &aggregationIterator);

	StringView collectionName = { 0 };
	bson_value_t pipelineValue = { 0 };
	pg_uuid_t *collectionUuid = NULL;

	bool explain = false;
	bool hasCursor = false;
	bool isCollectionAgnosticQuery = false;
	while (bson_iter_next(&aggregationIterator))
	{
		StringView keyView = bson_iter_key_string_view(&aggregationIterator);
		const bson_value_t *value = bson_iter_value(&aggregationIterator);

		if (StringViewEqualsCString(&keyView, "aggregate"))
		{
			if (BSON_ITER_HOLDS_NUMBER(&aggregationIterator) &&
				BsonValueAsDouble(value) == 1.0)
			{
				/* Collection agnostic aggregate */
				isCollectionAgnosticQuery = true;
			}
			else
			{
				EnsureTopLevelFieldType("aggregate", &aggregationIterator,
										BSON_TYPE_UTF8);
				collectionName.string = bson_iter_utf8(&aggregationIterator,
													   &collectionName.length);
			}
		}
		else if (StringViewEqualsCString(&keyView, "pipeline"))
		{
			EnsureTopLevelFieldType("pipeline", &aggregationIterator, BSON_TYPE_ARRAY);
			pipelineValue = *value;
		}
		else if (StringViewEqualsCString(&keyView, "cursor"))
		{
			hasCursor = true;
			ParseCursorDocument(&aggregationIterator, queryData);
		}
		else if (StringViewEqualsCString(&keyView, "allowDiskUse"))
		{
			/* We otherwise ignore this for now (TODO Support this) */
			EnsureTopLevelFieldType("allowDiskUse", &aggregationIterator, BSON_TYPE_BOOL);
		}
		else if (StringViewEqualsCString(&keyView, "explain"))
		{
			/* We otherwise ignore this for now */
			EnsureTopLevelFieldType("explain", &aggregationIterator, BSON_TYPE_BOOL);
			explain = bson_iter_bool(&aggregationIterator);
		}
		else if (StringViewEqualsCString(&keyView, "hint"))
		{
			/* We ignore this for now (TODO Support this) */
		}
		else if (StringViewEqualsCString(&keyView, "collectionUUID"))
		{
			EnsureTopLevelFieldType("collectionUUID", &aggregationIterator,
									BSON_TYPE_BINARY);
			if (value->value.v_binary.subtype != BSON_SUBTYPE_UUID ||
				value->value.v_binary.data_len != 16)
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"field collectionUUID must be of UUID type")));
			}

			collectionUuid = palloc(sizeof(pg_uuid_t));
			memcpy(&collectionUuid->data, value->value.v_binary.data, 16);
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("%*s is an unknown field",
								   keyView.length, keyView.string)));
		}
	}

	if (pipelineValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Required variable pipeline must be valid")));
	}

	if (collectionName.length == 0 && !isCollectionAgnosticQuery)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Required variables aggregate must be valid")));
	}

	Query *query;
	if (isCollectionAgnosticQuery)
	{
		query = GenerateBaseAgnosticQuery(database, &context);
	}
	else
	{
		query = GenerateBaseTableQuery(database, &collectionName, collectionUuid,
									   &context);
	}

	/* Remember the base query - this will be needed since we need to update the cursor function on the base RTE */
	Query *baseQuery = query;

	query = MutateQueryWithPipeline(query, &pipelineValue, &context);

	queryData->isStreamableCursor = !context.requiresPersistentCursor &&
									!isCollectionAgnosticQuery;

	/* CMD_MERGE is case when pipeline has output stage ($merge or $out) result will be always single batch. */
	if (query->commandType == CMD_MERGE)
	{
		queryData->isSingleBatch = true;
	}
	queryData->namespaceName = context.namespaceName;

	/* This is validated *after* the pipeline parsing happens */
	if (!hasCursor && !explain && addCursorParams)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"The 'cursor' option is required, except for aggregate with the explain argument")));
	}

	if (addCursorParams)
	{
		bool addCursorAsConst = false;
		AddCursorFunctionsToQuery(query, baseQuery, queryData, &context,
								  addCursorAsConst);
	}
	else if (EnableCursorsOnAggregationQueryRewrite)
	{
		bool addCursorAsConst = true;
		AddCursorFunctionsToQuery(query, baseQuery, queryData, &context,
								  addCursorAsConst);
	}

	return query;
}


/*
 * Applies a find spec against a query and expands it into the underlying SQL AST.
 */
Query *
GenerateFindQuery(Datum databaseDatum, pgbson *findSpec, QueryData *queryData, bool
				  addCursorParams)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = databaseDatum;

	bson_iter_t findIterator;
	PgbsonInitIterator(findSpec, &findIterator);

	StringView collectionName = { 0 };
	bool hasFind = false;
	bson_value_t filter = { 0 };
	bson_value_t limit = { 0 };
	bson_value_t projection = { 0 };
	bson_value_t sort = { 0 };
	bson_value_t skip = { 0 };
	pg_uuid_t *collectionUuid = NULL;

	/* For finds, we can generally query the shard directly if available. */
	context.allowShardBaseTable = true;
	while (bson_iter_next(&findIterator))
	{
		StringView keyView = bson_iter_key_string_view(&findIterator);
		const bson_value_t *value = bson_iter_value(&findIterator);

		if (StringViewEqualsCString(&keyView, "find"))
		{
			hasFind = true;
			EnsureTopLevelFieldType("find", &findIterator, BSON_TYPE_UTF8);
			collectionName.string = bson_iter_utf8(&findIterator, &collectionName.length);
		}
		else if (StringViewEqualsCString(&keyView, "filter"))
		{
			EnsureTopLevelFieldType("filter", &findIterator, BSON_TYPE_DOCUMENT);
			filter = *value;
		}
		else if (StringViewEqualsCString(&keyView, "limit"))
		{
			/* Validation handled in the stage processing */
			limit = *value;
		}
		else if (StringViewEqualsCString(&keyView, "projection"))
		{
			/* Validation handled in the stage processing */
			projection = *value;
		}
		else if (StringViewEqualsCString(&keyView, "skip"))
		{
			/* Validation handled in the stage processing */
			skip = *value;
		}
		else if (StringViewEqualsCString(&keyView, "sort"))
		{
			EnsureTopLevelFieldType("sort", &findIterator, BSON_TYPE_DOCUMENT);
			sort = *value;
		}
		else if (StringViewEqualsCString(&keyView, "singleBatch"))
		{
			EnsureTopLevelFieldType("singleBatch", &findIterator, BSON_TYPE_BOOL);
			if (value->value.v_bool)
			{
				queryData->isSingleBatch = true;
			}
		}
		else if (StringViewEqualsCString(&keyView, "batchSize"))
		{
			SetBatchSize("batchSize", value, queryData);
		}
		else if (StringViewEqualsCString(&keyView, "ntoreturn"))
		{
			SetBatchSize("ntoreturn", value, queryData);
		}
		else if (StringViewEqualsCString(&keyView, "hint") ||
				 StringViewEqualsCString(&keyView, "min") ||
				 StringViewEqualsCString(&keyView, "max") ||
				 StringViewEqualsCString(&keyView, "allowPartialResults") ||
				 StringViewEqualsCString(&keyView, "allowDiskUse") ||
				 StringViewEqualsCString(&keyView, "noCursorTimeout"))
		{
			/* We ignore this for now (TODO Support this?) */
		}
		else if ((StringViewEqualsCString(&keyView, "returnKey") ||
				  StringViewEqualsCString(&keyView, "showRecordId")) &&
				 BsonValueAsBool(value))
		{
			/* fail if returnKey or showRecordId are present and with boolean value true, else ignore */
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("key %.*s is not supported yet",
								   keyView.length, keyView.string),
							errhint("key %.*s is not supported yet",
									keyView.length, keyView.string)));
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string),
							errhint("%.*s is an unknown field",
									keyView.length, keyView.string)));
		}
	}

	if (!hasFind)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("Required element \"find\" missing.")));
	}
	else if (collectionName.length == 0)
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Collection name can't be empty.")));
	}

	/* Find supports negative limit (as well as count) */
	if (BsonValueIsNumber(&limit))
	{
		int64_t intValue = BsonValueAsInt64(&limit);
		if (intValue < 0)
		{
			limit.value.v_int64 = labs(intValue);
			limit.value_type = BSON_TYPE_INT64;
		}
		else if (intValue == 0)
		{
			limit.value_type = BSON_TYPE_EOD;
		}
	}

	/* TODO: In cases that need persisted cursors, we can't allow querying base table directly
	 * until we find a solution for persisted cursors here.
	 */
	if (sort.value_type != BSON_TYPE_EOD)
	{
		context.allowShardBaseTable = false;
		context.requiresPersistentCursor = true;
	}

	if (RequiresPersistentCursorSkip(&skip))
	{
		context.allowShardBaseTable = false;
		context.requiresPersistentCursor = true;
	}

	if (RequiresPersistentCursorLimit(&limit))
	{
		context.allowShardBaseTable = false;
		context.requiresPersistentCursor = true;
	}

	Query *query = GenerateBaseTableQuery(databaseDatum, &collectionName, collectionUuid,
										  &context);
	Query *baseQuery = query;

	/* First apply match */
	if (filter.value_type != BSON_TYPE_EOD)
	{
		query = HandleMatch(&filter, query, &context);
		context.stageNum++;
	}

	/* Then apply sort */
	if (sort.value_type != BSON_TYPE_EOD)
	{
		query = HandleSort(&sort, query, &context);
		context.stageNum++;
	}

	/* Then do skip and then limit */
	if (skip.value_type != BSON_TYPE_EOD)
	{
		query = HandleSkip(&skip, query, &context);
		context.stageNum++;
	}

	if (limit.value_type != BSON_TYPE_EOD)
	{
		query = HandleLimit(&limit, query, &context);
		context.stageNum++;
	}

	/* finally update projection */
	if (projection.value_type != BSON_TYPE_EOD)
	{
		query = HandleProjectFind(&projection, &filter, query, &context);
	}

	/* $near and $nearSphere add sort clause to query, for them we need persistent cursor. */
	if (query->sortClause)
	{
		context.requiresPersistentCursor = true;
	}

	queryData->isStreamableCursor = !context.requiresPersistentCursor;
	queryData->namespaceName = context.namespaceName;
	if (addCursorParams)
	{
		bool addCursorAsConst = false;
		AddCursorFunctionsToQuery(query, baseQuery, queryData, &context,
								  addCursorAsConst);
	}
	else if (EnableCursorsOnAggregationQueryRewrite)
	{
		bool addCursorAsConst = true;
		AddCursorFunctionsToQuery(query, baseQuery, queryData, &context,
								  addCursorAsConst);
	}

	return query;
}


/*
 * Generates a query that is akin to the MongoDB $count query command
 */
Query *
GenerateCountQuery(Datum databaseDatum, pgbson *countSpec)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = databaseDatum;

	bson_iter_t countIterator;
	PgbsonInitIterator(countSpec, &countIterator);

	StringView collectionName = { 0 };
	bson_value_t filter = { 0 };
	bson_value_t limit = { 0 };
	bson_value_t skip = { 0 };
	pg_uuid_t *collectionUuid = NULL;

	bool hasQueryModifier = false;
	while (bson_iter_next(&countIterator))
	{
		StringView keyView = bson_iter_key_string_view(&countIterator);
		const bson_value_t *value = bson_iter_value(&countIterator);

		if (StringViewEqualsCString(&keyView, "count"))
		{
			EnsureTopLevelFieldType("count", &countIterator, BSON_TYPE_UTF8);
			collectionName.string = bson_iter_utf8(&countIterator,
												   &collectionName.length);
		}
		else if (StringViewEqualsCString(&keyView, "query"))
		{
			EnsureTopLevelFieldType("query", &countIterator, BSON_TYPE_DOCUMENT);
			filter = *value;
			hasQueryModifier = true;
		}
		else if (StringViewEqualsCString(&keyView, "limit"))
		{
			/* Validation handled in the stage processing */
			limit = *value;
			hasQueryModifier = true;
		}
		else if (StringViewEqualsCString(&keyView, "skip"))
		{
			/* Validation handled in the stage processing */
			skip = *value;
			hasQueryModifier = true;
		}
		else if (StringViewEqualsCString(&keyView, "hint") ||
				 StringViewEqualsCString(&keyView, "fields"))
		{
			/* We ignore this for now (TODO Support this?) */
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string)));
		}
	}

	if (collectionName.length == 0)
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Collection name can't be empty.")));
	}

	Query *query = GenerateBaseTableQuery(databaseDatum, &collectionName, collectionUuid,
										  &context);

	/*
	 * the count() query which has no filter/skip/limit/etc can be done via an estimatedDocumentCount
	 * per the mongo spec. In this case, we rewrite the query as a collStats aggregation query with
	 * a project to make it the appropriate output.
	 */
	if (!hasQueryModifier && context.mongoCollection != NULL)
	{
		/* Collection exists, get a collStats: { "count": {} } */
		pgbson_writer collStatsWriter;
		pgbson_writer countWriter;
		PgbsonWriterInit(&collStatsWriter);
		PgbsonWriterStartDocument(&collStatsWriter, "count", 5, &countWriter);
		PgbsonWriterEndDocument(&collStatsWriter, &countWriter);
		pgbson *collStatsSpec = PgbsonWriterGetPgbson(&collStatsWriter);
		bson_value_t collStatsSpecValue = ConvertPgbsonToBsonValue(collStatsSpec);
		query = HandleCollStats(&collStatsSpecValue, query, &context);
		context.stageNum++;

		/* Add a $project where the count field is written as 'n' */
		pgbson_writer projectWriter;
		PgbsonWriterInit(&projectWriter);
		PgbsonWriterAppendUtf8(&projectWriter, "n", 1, "$count");
		pgbson *projectSpec = PgbsonWriterGetPgbson(&projectWriter);
		bson_value_t projectSpecValue = ConvertPgbsonToBsonValue(projectSpec);
		query = HandleProject(&projectSpecValue, query, &context);
	}
	else
	{
		/* First apply match */
		if (filter.value_type != BSON_TYPE_EOD)
		{
			query = HandleMatch(&filter, query, &context);
			context.stageNum++;
		}

		/* Then do skip and then limit */
		if (skip.value_type != BSON_TYPE_EOD)
		{
			query = HandleSkip(&skip, query, &context);
			context.stageNum++;
			context.requiresPersistentCursor = true;
		}

		/* Count supports negative limit */
		if (BsonValueIsNumber(&limit))
		{
			int64_t intValue = BsonValueAsInt64(&limit);
			if (intValue < 0)
			{
				limit.value.v_int64 = labs(intValue);
				limit.value_type = BSON_TYPE_INT64;
			}
			else if (intValue == 0)
			{
				limit.value_type = BSON_TYPE_EOD;
			}
		}

		if (limit.value_type != BSON_TYPE_EOD)
		{
			query = HandleLimit(&limit, query, &context);
			context.stageNum++;
			if (RequiresPersistentCursorLimit(&limit))
			{
				context.requiresPersistentCursor = true;
			}
		}

		/* Now add a count stage that writes to the field "n" */
		bson_value_t countValue = { 0 };
		countValue.value_type = BSON_TYPE_UTF8;
		countValue.value.v_utf8.len = 1;
		countValue.value.v_utf8.str = "n";
		query = HandleCount(&countValue, query, &context);
	}

	/* Now add the "ok": 1 as an add fields stage. */
	pgbson_writer addFieldsWriter;
	PgbsonWriterInit(&addFieldsWriter);
	PgbsonWriterAppendDouble(&addFieldsWriter, "ok", 2, 1);
	pgbson *addFieldsSpec = PgbsonWriterGetPgbson(&addFieldsWriter);
	bson_value_t addFieldsValue = ConvertPgbsonToBsonValue(addFieldsSpec);
	query = HandleSimpleProjectionStage(&addFieldsValue, query, &context, "$addFields",
										GetMergeDocumentsFunctionOid());

	return query;
}


/*
 * Generates a query that is akin to the MongoDB $distinct query command
 */
Query *
GenerateDistinctQuery(Datum databaseDatum, pgbson *distinctSpec)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = databaseDatum;

	bson_iter_t distinctIter;
	PgbsonInitIterator(distinctSpec, &distinctIter);

	StringView collectionName = { 0 };
	bool hasDistinct = false;
	bson_value_t filter = { 0 };
	StringView distinctKey = { 0 };
	pg_uuid_t *collectionUuid = NULL;

	while (bson_iter_next(&distinctIter))
	{
		StringView keyView = bson_iter_key_string_view(&distinctIter);
		const bson_value_t *value = bson_iter_value(&distinctIter);

		if (StringViewEqualsCString(&keyView, "distinct"))
		{
			hasDistinct = true;
			EnsureTopLevelFieldType("distinct", &distinctIter, BSON_TYPE_UTF8);
			collectionName.string = bson_iter_utf8(&distinctIter, &collectionName.length);
		}
		else if (StringViewEqualsCString(&keyView, "query"))
		{
			if (!BSON_ITER_HOLDS_NULL(&distinctIter))
			{
				EnsureTopLevelFieldType("query", &distinctIter, BSON_TYPE_DOCUMENT);
				filter = *value;
			}
		}
		else if (StringViewEqualsCString(&keyView, "key"))
		{
			/* Validation handled in the stage processing */
			EnsureTopLevelFieldType("key", &distinctIter, BSON_TYPE_UTF8);
			distinctKey.string = bson_iter_utf8(&distinctIter, &distinctKey.length);
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string)));
		}
	}

	if (!hasDistinct)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("Required element \"distinct\" missing.")));
	}
	else if (collectionName.length == 0)
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Collection name can't be empty.")));
	}

	if (distinctKey.length == 0)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("distinct key can't be empty.")));
	}

	if (strlen(distinctKey.string) != distinctKey.length)
	{
		ereport(ERROR, (errcode(MongoKeyCannotContainNullByte),
						errmsg("Distinct key cannot have embedded nulls")));
	}

	Query *query = GenerateBaseTableQuery(databaseDatum, &collectionName, collectionUuid,
										  &context);

	/* First apply match */
	if (filter.value_type != BSON_TYPE_EOD)
	{
		query = HandleMatch(&filter, query, &context);
		context.stageNum++;
	}

	query = HandleDistinct(&distinctKey, query, &context);

	return query;
}


/*
 * Parses the GetMore wire protocol spec and returns the cursorId associated
 * with it. Also updates the queryData with cursor related information.
 */
int64_t
ParseGetMore(text *databaseName, pgbson *getMoreSpec, QueryData *queryData)
{
	bson_iter_t cursorSpecIter;
	PgbsonInitIterator(getMoreSpec, &cursorSpecIter);
	int64_t cursorId = 0;
	while (bson_iter_next(&cursorSpecIter))
	{
		const char *pathKey = bson_iter_key(&cursorSpecIter);

		if (strcmp(pathKey, "getMore") == 0)
		{
			EnsureTopLevelFieldType("getMore", &cursorSpecIter, BSON_TYPE_INT64);
			const bson_value_t *value = bson_iter_value(&cursorSpecIter);
			cursorId = BsonValueAsInt64(value);
		}
		else if (strcmp(pathKey, "batchSize") == 0)
		{
			const bson_value_t *value = bson_iter_value(&cursorSpecIter);
			SetBatchSize("batchSize", value, queryData);
		}
		else if (strcmp(pathKey, "collection") == 0)
		{
			const bson_value_t *value = bson_iter_value(&cursorSpecIter);
			EnsureTopLevelFieldValueType("collection", value, BSON_TYPE_UTF8);
			StringView nameView = {
				.string = value->value.v_utf8.str,
				.length = value->value.v_utf8.len
			};
			queryData->namespaceName = CreateNamespaceName(databaseName,
														   &nameView);
		}
		else if (!IsCommonSpecIgnoredField(pathKey))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("%s is an unknown field",
								   pathKey)));
		}
	}

	if (queryData->namespaceName == NULL)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("Required element \"collection\" missing.")));
	}

	if (cursorId == 0)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("Required element \"getMore\" missing.")));
	}

	return cursorId;
}


/*
 * Given an aggregation pipeline that is specified for a
 * $lookup Stage, validates whether the stages can be
 * safely pulled up to the top level join.
 * This is true if there's no intersection between those
 * stages and the lookup's localField which is specified
 * on the right table.
 */
bool
CanInlineLookupPipeline(const bson_value_t *pipeline,
						const StringView *lookupPath)
{
	bson_iter_t pipelineIter;
	BsonValueInitIterator(pipeline, &pipelineIter);

	while (bson_iter_next(&pipelineIter))
	{
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIter))
		{
			continue;
		}

		const bson_value_t *stageValue = bson_iter_value(&pipelineIter);
		pgbsonelement stageElement;
		if (!TryGetBsonValueToPgbsonElement(stageValue, &stageElement))
		{
			continue;
		}

		/* Now handle each stage */
		AggregationStageDefinition *definition = (AggregationStageDefinition *) bsearch(
			stageElement.path, StageDefinitions,
			AggregationStageCount,
			sizeof(AggregationStageDefinition),
			CompareStageByStageName);
		if (definition == NULL)
		{
			continue;
		}

		/* The inline function is not specified for this stage,
		 * We can't inline.
		 */
		if (definition->canInlineLookupStageFunc == NULL)
		{
			return false;
		}

		if (!definition->canInlineLookupStageFunc(&stageElement.bsonValue, lookupPath))
		{
			return false;
		}
	}

	return true;
}


/*
 * Processes simple projections (dollar_project, dollar_add_fields etc).
 * This just updates the targetEntry with the new projector.
 */
Query *
HandleSimpleProjectionStage(const bson_value_t *existingValue, Query *query,
							AggregationPipelineBuildContext *context,
							const char *stageName, Oid functionOid)
{
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40272),
						errmsg("%s specification stage must be an object", stageName)));
	}

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(query->targetList);

	Expr *currentProjection = firstEntry->expr;

	/* Projection followed by projection - try to inline */
	if (TryInlineProjection((Node *) currentProjection, functionOid, existingValue))
	{
		return query;
	}

	pgbson *docBson = PgbsonInitFromBuffer(
		(char *) existingValue->value.v_doc.data,
		existingValue->value.v_doc.data_len);
	Const *addFieldsProcessed = MakeBsonConst(docBson);

	List *args = list_make2(currentProjection, addFieldsProcessed);
	FuncExpr *resultExpr = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	firstEntry->expr = (Expr *) resultExpr;
	return query;
}


/*
 * Mutates the query for the $addFields stage
 */
static Query *
HandleAddFields(const bson_value_t *existingValue, Query *query,
				AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_ADD_FIELDS);
	return HandleSimpleProjectionStage(existingValue, query, context, "$addFields",
									   BsonDollarAddFieldsFunctionOid());
}


/*
 * Handles the $bucket stage.
 * Converts to a $group stage with $_bucketInternal operator to handle bucket specific logics.
 * See bucket.md for more details.
 */
static Query *
HandleBucket(const bson_value_t *existingValue, Query *query,
			 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_BUCKET);

	bson_value_t groupSpec = { 0 };
	RewriteBucketGroupSpec(existingValue, &groupSpec);
	query = HandleGroup(&groupSpec, query, context);

	return query;
}


/*
 * Mutates the query for the $project stage
 */
static Query *
HandleProject(const bson_value_t *existingValue, Query *query,
			  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_PROJECT);
	return HandleSimpleProjectionStage(existingValue, query, context, "$project",
									   BsonDollarProjectFunctionOid());
}


static Query *
HandleProjectFind(const bson_value_t *existingValue, const bson_value_t *queryValue,
				  Query *query,
				  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_PROJECT_FIND);
	EnsureTopLevelFieldValueType("projection", existingValue, BSON_TYPE_DOCUMENT);

	if (IsBsonValueEmptyDocument(existingValue))
	{
		return query;
	}

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(query->targetList);

	Expr *currentProjection = firstEntry->expr;
	pgbson *docBson = PgbsonInitFromBuffer(
		(char *) existingValue->value.v_doc.data,
		existingValue->value.v_doc.data_len);
	Const *projectProcessed = MakeBsonConst(docBson);

	List *args;
	if (queryValue->value_type == BSON_TYPE_EOD)
	{
		args = list_make2(currentProjection, projectProcessed);
	}
	else
	{
		pgbson *queryDoc = PgbsonInitFromBuffer(
			(char *) queryValue->value.v_doc.data,
			queryValue->value.v_doc.data_len);
		Const *queryDocProcessed = MakeBsonConst(queryDoc);
		args = list_make3(currentProjection, projectProcessed, queryDocProcessed);
	}


	FuncExpr *resultExpr = makeFuncExpr(
		BsonDollarProjectFindFunctionOid(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	firstEntry->expr = (Expr *) resultExpr;
	return query;
}


/*
 * Mutates the query for the $replaceRoot stage
 */
static Query *
HandleReplaceRoot(const bson_value_t *existingValue, Query *query,
				  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_REPLACE_ROOT);
	return HandleSimpleProjectionStage(existingValue, query, context, "$replaceRoot",
									   BsonDollarReplaceRootFunctionOid());
}


/*
 * Mutates the query for the $skip stage.
 * If there is a limit, then injects a new subquery and sets the skip
 * since Skip is processed before limit in PG.
 */
static Query *
HandleSkip(const bson_value_t *existingValue, Query *query,
		   AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_SKIP);
	if (!BsonValueIsNumber(existingValue))
	{
		ereport(ERROR, (errcode(MongoLocation15972),
						errmsg("Argument to $skip must be a number")));
	}

	bool checkFixedInteger = true;
	if (!IsBsonValue64BitInteger(existingValue, checkFixedInteger))
	{
		double doubleValue = BsonValueAsDouble(existingValue);
		ereport(ERROR, (errcode(MongoLocation5107200),
						errmsg(
							"invalid argument to $skip stage: Cannot represent as a 64-bit integer: $skip: %f",
							doubleValue)));
	}

	int64_t skipValue = BsonValueAsInt64(existingValue);
	if (skipValue < 0)
	{
		ereport(ERROR, (errcode(MongoLocation5107200),
						errmsg(
							"invalid argument to $skip stage: Expected a non - negative number in: $skip: %ld",
							skipValue)));
	}

	if (skipValue == 0)
	{
		/* Skip 0 can be ignored */
		return query;
	}

	if (query->limitCount != NULL)
	{
		/* Because skip is evaluated first, an existing limit implies a new stage */
		query = MigrateQueryToSubQuery(query, context);
	}

	if (query->limitOffset != NULL)
	{
		Assert(IsA(query->limitOffset, Const));
		Const *limitOffset = (Const *) query->limitOffset;

		/* We already have a skip: Sum up the skips */
		limitOffset->constvalue = Int64GetDatum(skipValue + DatumGetInt64(
													limitOffset->constvalue));
	}
	else
	{
		query->limitOffset = (Node *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
												Int64GetDatum(skipValue), false, true);
	}

	/* Postgres applies OFFSET after other layers. Mongo applies it first. to emulate this we need to
	 * Push down a subquery.
	 */
	context->requiresSubQuery = true;

	return query;
}


/*
 * Mutates the query for the $limit stage
 * Simply updates the limit in the current query.
 */
static Query *
HandleLimit(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_LIMIT);
	if (!BsonValueIsNumber(existingValue))
	{
		ereport(ERROR, (errcode(MongoLocation15957),
						errmsg("the limit must be specified as a number")));
	}

	bool checkFixedInteger = true;
	if (!IsBsonValue64BitInteger(existingValue, checkFixedInteger))
	{
		double doubleValue = BsonValueAsDouble(existingValue);
		ereport(ERROR, (errcode(MongoLocation5107201),
						errmsg(
							"invalid argument to $limit stage: Cannot represent as a 64-bit integer: $limit: %f",
							doubleValue)));
	}

	int64_t limitValue = BsonValueAsInt64(existingValue);
	if (limitValue < 0)
	{
		ereport(ERROR, (errcode(MongoLocation5107201),
						errmsg(
							"invalid argument to $skip stage: Expected a non - negative number in: $limit: %ld",
							limitValue)));
	}

	if (limitValue == 0)
	{
		ereport(ERROR, (errcode(MongoLocation15958),
						errmsg("the limit must be positive")));
	}

	if (query->limitCount != NULL)
	{
		Assert(IsA(query->limitCount, Const));
		Const *limitConst = (Const *) query->limitCount;

		/* We just take the min of the two */
		limitConst->constvalue = Int64GetDatum(Min(limitValue, DatumGetInt64(
													   limitConst->constvalue)));
	}
	else
	{
		query->limitCount = (Node *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
											   Int64GetDatum(limitValue), false, true);
	}

	/* PG applies projection before LIMIT - consequently if there was an error in the 11th
	 * document and you do limit 10, PG would error out, but Mongo would not.
	 * This is a nuance that requires a subquery.
	 */
	context->requiresSubQuery = true;

	return query;
}


/*
 * Mutates the query for the $match stage.
 * Simply calls the ExpandQueryOperator similar to the @@ operator.
 * Also injects a new stage if ther is a LIMIT or a SKIP since the
 * match needs to apply post-limit.
 * Applies the match on the current projector.
 */
Query *
HandleMatch(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_MATCH);
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation15959),
						errmsg("the match filter must be an expression in an object")));
	}

	if (query->limitOffset != NULL || query->limitCount != NULL)
	{
		query = MigrateQueryToSubQuery(query, context);
	}

	TargetEntry *entry = linitial(query->targetList);
	BsonQueryOperatorContext filterContext = { 0 };
	filterContext.documentExpr = entry->expr;
	filterContext.inputType = MongoQueryOperatorInputType_Bson;
	filterContext.simplifyOperators = true;
	filterContext.coerceOperatorExprIfApplicable = true;
	filterContext.requiredFilterPathNameHashSet = context->requiredFilterPathNameHashSet;

	bson_iter_t queryDocIterator;
	BsonValueInitIterator(existingValue, &queryDocIterator);
	List *quals = CreateQualsFromQueryDocIterator(&queryDocIterator, &filterContext);

	UpdateQueryOperatorContextSortList(query, filterContext.sortClauses,
									   filterContext.targetEntries);

	quals = AddShardKeyAndIdFilters(existingValue, query, context, entry, quals);
	if (query->jointree->quals != NULL)
	{
		quals = lappend(quals, query->jointree->quals);
	}

	query->jointree->quals = (Node *) make_ands_explicit(quals);
	return query;
}


static List *
AddShardKeyAndIdFilters(const bson_value_t *existingValue, Query *query,
						AggregationPipelineBuildContext *context,
						TargetEntry *entry, List *existingQuals)
{
	if (context->mongoCollection == NULL || !IsA(entry->expr, Var))
	{
		return existingQuals;
	}

	Var *var = (Var *) entry->expr;
	if (var->varlevelsup != 0)
	{
		return existingQuals;
	}

	RangeTblEntry *rtable = rt_fetch(var->varno, query->rtable);

	/* Check that we're still on the base RTE */
	if (rtable->rtekind != RTE_RELATION ||
		rtable->relid != context->mongoCollection->relationId)
	{
		return existingQuals;
	}

	/* If we have a shardkey, add it */
	bool hasShardKeyFilters = false;
	if (context->mongoCollection->shardKey != NULL)
	{
		Expr *shardKeyFilters =
			CreateShardKeyFiltersForQuery(existingValue,
										  context->mongoCollection->shardKey,
										  context->mongoCollection->collectionId,
										  var->varno);
		if (shardKeyFilters != NULL)
		{
			hasShardKeyFilters = true;
			existingQuals = lappend(existingQuals, shardKeyFilters);
		}
	}
	else
	{
		hasShardKeyFilters = true;
	}

	if (hasShardKeyFilters)
	{
		Expr *idFilter = CreateIdFilterForQuery(existingQuals, var->varno);
		if (idFilter != NULL)
		{
			existingQuals = lappend(existingQuals, idFilter);
		}
	}

	return existingQuals;
}


/*
 * Applies the $unset operator to the query.
 * Converts to a $project exclude.
 */
static Query *
HandleUnset(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_UNSET);
	if (existingValue->value_type != BSON_TYPE_UTF8 &&
		existingValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation31002),
						errmsg("$unset specification must be a string or an array")));
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		if (existingValue->value.v_utf8.len == 0)
		{
			ereport(ERROR, (errcode(MongoLocation40352),
							errmsg("FieldPath cannot be constructed with empty string")));
		}

		if (existingValue->value.v_utf8.str[0] == '$')
		{
			ereport(ERROR, (errcode(MongoLocation16410),
							errmsg("FieldPath field names may not start with '$'")));
		}

		/* Add it as an exclude path */
		PgbsonWriterAppendInt32(&writer, existingValue->value.v_utf8.str,
								existingValue->value.v_utf8.len, 0);
	}
	else
	{
		bson_iter_t valueIterator;
		BsonValueInitIterator(existingValue, &valueIterator);

		while (bson_iter_next(&valueIterator))
		{
			const bson_value_t *arrayValue = bson_iter_value(&valueIterator);
			if (arrayValue->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoLocation31120),
								errmsg(
									"$unset specification must be a string or an array containing only string values")));
			}

			if (arrayValue->value.v_utf8.len == 0)
			{
				ereport(ERROR, (errcode(MongoLocation40352),
								errmsg(
									"FieldPath cannot be constructed with empty string")));
			}

			if (arrayValue->value.v_utf8.str[0] == '$')
			{
				ereport(ERROR, (errcode(MongoLocation16410),
								errmsg("FieldPath field names may not start with '$'")));
			}

			/* Add it as an exclude path */
			PgbsonWriterAppendInt32(&writer, arrayValue->value.v_utf8.str,
									arrayValue->value.v_utf8.len, 0);
		}
	}

	pgbson *excludeBson = PgbsonWriterGetPgbson(&writer);

	if (IsPgbsonEmptyDocument(excludeBson))
	{
		ereport(ERROR, (errcode(MongoLocation31119),
						errmsg(
							"$unset specification must be a string or an array with at least one field")));
	}

	Const *addFieldsProcessed = MakeBsonConst(excludeBson);

	TargetEntry *firstEntry = linitial(query->targetList);
	Expr *currentProjection = firstEntry->expr;
	List *args = list_make2(currentProjection, addFieldsProcessed);
	FuncExpr *resultExpr = makeFuncExpr(
		BsonDollarProjectFunctionOid(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	firstEntry->expr = (Expr *) resultExpr;
	return query;
}


/*
 * Applies the Unwind operator to the query.
 * Requests a new subquery stage (since we process unwind as an SRF)
 * after the unwind to ensure subsequent stages can process it on a
 * per tuple basis.
 */
static Query *
HandleUnwind(const bson_value_t *existingValue, Query *query,
			 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_UNWIND);
	bson_value_t pathValue = { 0 };
	bool hasOptions = false;
	switch (existingValue->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			pathValue = *existingValue;
			break;
		}

		case BSON_TYPE_DOCUMENT:
		{
			hasOptions = true;
			bson_iter_t optionsDocIter;
			BsonValueInitIterator(existingValue, &optionsDocIter);
			while (bson_iter_next(&optionsDocIter))
			{
				const char *key = bson_iter_key(&optionsDocIter);
				const bson_value_t *value = bson_iter_value(&optionsDocIter);
				if (strcmp(key, "path") == 0)
				{
					pathValue = *value;
				}
				else if (strcmp(key, "includeArrayIndex") == 0)
				{
					if (value->value_type != BSON_TYPE_UTF8)
					{
						ereport(ERROR, (errcode(MongoLocation28810),
										errmsg(
											"expected a non-empty string for the includeArrayIndex option to $unwind stage")));
					}

					StringView includeArrayIndexView = (StringView) {
						.string = value->value.v_utf8.str,
						.length = value->value.v_utf8.len
					};

					if (includeArrayIndexView.length == 0)
					{
						ereport(ERROR, (errcode(MongoLocation28810),
										errmsg(
											"expected a non-empty string for the includeArrayIndex option to $unwind stage")));
					}

					if (StringViewStartsWith(&includeArrayIndexView, '$'))
					{
						ereport(ERROR, (errcode(MongoLocation28822),
										errmsg(
											"includeArrayIndex option to $unwind stage should not be prefixed with a '$': %s",
											includeArrayIndexView.string)));
					}
				}
				else if (strcmp(key, "preserveNullAndEmptyArrays") == 0)
				{
					if (value->value_type != BSON_TYPE_BOOL)
					{
						ereport(ERROR, (errcode(MongoLocation28809),
										errmsg(
											"expected a boolean for the preserveNullAndEmptyArrays option to $unwind stage")));
					}
				}
				else
				{
					ereport(ERROR, (errcode(MongoLocation28811),
									errmsg("unrecognized option to $unwind stage")));
				}
			}

			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoLocation15981),
							errmsg(
								"expected either a string or an object as specification for $unwind stage, got %s",
								BsonTypeName(existingValue->value_type))));
		}
	}

	if (pathValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation28812),
						errmsg("No path specified to $unwind stage")));
	}
	if (pathValue.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoLocation28808),
						errmsg("Expected a string as the path for $unwind stage, got %s",
							   BsonTypeName(pathValue.value_type))));
	}

	StringView pathView = {
		.string = pathValue.value.v_utf8.str,
		.length = pathValue.value.v_utf8.len
	};
	if (pathView.length == 0)
	{
		ereport(ERROR, (errcode(MongoLocation28812),
						errmsg("No path specified to $unwind stage")));
	}

	if (!StringViewStartsWith(&pathView, '$') || pathView.length == 1)
	{
		ereport(ERROR, (errcode(MongoLocation28818),
						errmsg(
							"path option to $unwind stage should be prefixed with a '$': %.*s",
							pathView.length, pathView.string)));
	}

	if (pathView.string[1] == '$')
	{
		ereport(ERROR, (errcode(MongoLocation16410),
						errmsg("FieldPath field names may not start with '$'.")));
	}


	FuncExpr *resultExpr;

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(query->targetList);
	Expr *currentProjection = firstEntry->expr;
	if (hasOptions)
	{
		Const *unwindValue = MakeBsonConst(PgbsonInitFromDocumentBsonValue(
											   existingValue));
		List *args = list_make2(currentProjection, unwindValue);
		resultExpr = makeFuncExpr(
			BsonDollarUnwindWithOptionsFunctionOid(), BsonTypeId(), args, InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
	}
	else
	{
		Const *unwindValue = MakeTextConst(existingValue->value.v_utf8.str,
										   existingValue->value.v_utf8.len);
		List *args = list_make2(currentProjection, unwindValue);
		resultExpr = makeFuncExpr(
			BsonDollarUnwindFunctionOid(), BsonTypeId(), args, InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
	}

	resultExpr->funcretset = true;
	firstEntry->expr = (Expr *) resultExpr;
	query->hasTargetSRFs = true;

	/* Since this is an SRF, we need a child CTE if anyone comes after an unwind */
	context->requiresSubQuery = true;
	return query;
}


/*
 * Helper function to handle making an aggregate function projector with a single
 * argument. e.g. BSONMAX(document), BSONSUM(document)
 * borrows code from the pg parser (ParseFuncOrColumn in parse_func.c)
 */
static Aggref *
CreateSingleArgAggregate(Oid aggregateFunctionId, Expr *argument, ParseState *parseState)
{
	List *aggregateArgs = list_make1(argument);
	Aggref *aggref = makeNode(Aggref);
	aggref->aggfnoid = aggregateFunctionId;
	aggref->aggtype = BsonTypeId();
	aggref->aggtranstype = InvalidOid; /* set by planner later */
	aggref->aggdirectargs = NIL; /* count has no direct args */
	aggref->aggkind = AGGKIND_NORMAL; /* reset by planner */
	/* aggref->agglevelsup = 0 / * > 0 if agg belongs to outer query * / */
	aggref->aggsplit = AGGSPLIT_SIMPLE;
	aggref->aggno = -1;     /* planner will set aggno and aggtransno */
	aggref->aggtransno = -1;
	aggref->location = -1;
	aggref->aggargtypes = list_make1_oid(BsonTypeId());


	bool aggDistinct = false;
	transformAggregateCall(parseState, aggref, aggregateArgs, NIL, aggDistinct);
	return aggref;
}


static Query *
HandleDistinct(const StringView *distinctKey, Query *query,
			   AggregationPipelineBuildContext *context)
{
	FuncExpr *resultExpr;

	/* If there are existing sort clauses in the query, can happen with $near / $nearSphere query operators
	 * push the query down as a subquery.
	 */
	if (query->sortClause != NULL)
	{
		query = MigrateQueryToSubQuery(query, context);
	}

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(query->targetList);
	Expr *currentProjection = firstEntry->expr;
	Const *unwindValue = MakeTextConst(distinctKey->string,
									   distinctKey->length);
	List *args = list_make2(currentProjection, unwindValue);

	/* Create a distinct unwind - to expand arrays and such */
	resultExpr = makeFuncExpr(
		BsonDistinctUnwindFunctionOid(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	resultExpr->funcretset = true;
	firstEntry->expr = (Expr *) resultExpr;
	query->hasTargetSRFs = true;

	/* Add the distinct */
	SortGroupClause *distinctSortGroup = makeNode(SortGroupClause);
	distinctSortGroup->eqop = BsonEqualOperatorId();
	distinctSortGroup->sortop = BsonLessThanOperatorId();
	distinctSortGroup->tleSortGroupRef = assignSortGroupRef(firstEntry,
															query->targetList);
	query->distinctClause = list_make1(distinctSortGroup);

	/* Add the bson_distinct_agg function */
	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_SELECT_TARGET;
	parseState->p_next_resno = firstEntry->resno + 1;

	query = MigrateQueryToSubQuery(query, context);
	firstEntry = linitial(query->targetList);
	Aggref *aggref = CreateSingleArgAggregate(BsonDistinctAggregateFunctionOid(),
											  firstEntry->expr, parseState);

	firstEntry->expr = (Expr *) aggref;
	query->hasAggs = true;
	return query;
}


static Query *
HandleGeoNear(const bson_value_t *existingValue, Query *query,
			  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_GEONEAR);

	EnsureGeospatialFeatureEnabled();

	if (!IsClusterVersionAtleastThis(1, 17, 2))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg(
							"$geoNear is not supported yet.")));
	}

	if (context->stageNum != 0)
	{
		ereport(ERROR, (errcode(MongoLocation40603),
						errmsg(
							"$geoNear was not the first stage in the pipeline.")));
	}


	RangeTblEntry *rte = linitial(query->rtable);
	if (rte->rtekind != RTE_RELATION)
	{
		ereport(ERROR, (
					errcode(MongoBadValue),
					errmsg("$geoNear is only supported on collections."),
					errhint("$geoNear is only supported on collections. RTE KIND: %d",
							rte->rtekind)));
	}

	const pgbson *geoNearQueryDoc = PgbsonInitFromDocumentBsonValue(existingValue);

	/*
	 * Create a $geoNear query of this form:
	 *
	 * For 2dsphere index:
	 *		SELECT bson_dollar_project_geonear(document, <geoNearSpec>) AS document
	 *		FROM <collection> WHERE bson_validate_geography(document, <geoNearSpec.key>) IS NOT NULL AND
	 *      [optional] document <|-|> <geoNearSpec> <= maxDistance AND document <|-|> <geoNearSpec> >= minDistance
	 *		ORDER BY bson_validate_geography(document, <geoNearSpec.key>) <|-|> <geoNearSpec>;
	 *
	 * For 2d index:
	 *      SELECT bson_dollar_project_geonear(document, <geoNearSpec>) AS document
	 *      FROM <collection> WHERE bson_validate_geometry(document, <geoNearSpec.key>) IS NOT NULL
	 *      [optional] document <|-|> <geoNearSpec> <= maxDistance AND document <|-|> <geoNearSpec> >= minDistance
	 *      ORDER BY bson_validate_geometry(document, <geoNearSpec.key>) <|-|> <geoNearSpec>;
	 *
	 * The <|-|> operator is a custom operator that compares the distance between two geometries/geographies
	 * based on the geoNear requirements.
	 *
	 */
	Const *queryConst = makeConst(BsonTypeId(), -1, InvalidOid, -1, PointerGetDatum(
									  geoNearQueryDoc),
								  false, false);

	TargetEntry *firstEntry = linitial(query->targetList);
	Var *docExpr = (Var *) firstEntry->expr;

	GeonearRequest *request = ParseGeonearRequest(geoNearQueryDoc);

	/* Add any query filters available */
	if (request->query.value_type != BSON_TYPE_EOD)
	{
		query = HandleMatch(&(request->query), query, context);
		ValidateQueryOperatorsForGeoNear((Node *) query->jointree->quals, NULL);

		if (TargetListContainsGeonearOp(query->targetList))
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("Too many geoNear expressions")));
		}
	}

	TargetEntry *sortTargetEntry;
	SortGroupClause *sortGroupClause;
	List *quals = CreateExprForGeonearAndNearSphere(geoNearQueryDoc, (Expr *) docExpr,
													request, &sortTargetEntry,
													&sortGroupClause);

	/* Update the resno and sortgroup ref based on the query */
	UpdateQueryOperatorContextSortList(query, list_make1(sortGroupClause),
									   list_make1(sortTargetEntry));

	if (query->jointree->quals != NULL)
	{
		quals = lappend(quals, query->jointree->quals);
	}

	query->jointree->quals = (Node *) make_ands_explicit(quals);

	/* Add the geoNear projection function */
	FuncExpr *projectionExpr = makeFuncExpr(
		BsonDollarProjectGeonearFunctionOid(), BsonTypeId(), list_make2(docExpr,
																		queryConst),
		InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	firstEntry->expr = (Expr *) projectionExpr;

	if (context->nestedPipelineLevel > 0)
	{
		/* For nested pipelines this will require a subquery */
		context->requiresSubQueryAfterProject = true;
	}

	return query;
}


/*
 * Creates a multi-argument aggregate based on the provided function id.
 * e.g. BSON_ARRAY_AGG(bson, text)
 */
Aggref *
CreateMultiArgAggregate(Oid aggregateFunctionId, List *args, List *argTypes,
						ParseState *parseState)
{
	Aggref *aggref = makeNode(Aggref);
	aggref->aggfnoid = aggregateFunctionId;
	aggref->aggtype = BsonTypeId();
	aggref->aggtranstype = InvalidOid; /* set by planner later */
	aggref->aggdirectargs = NIL; /* count has no direct args */
	aggref->aggkind = AGGKIND_NORMAL; /* reset by planner */
	/* aggref->agglevelsup = 0 / * > 0 if agg belongs to outer query * / */
	aggref->aggsplit = AGGSPLIT_SIMPLE;
	aggref->aggno = -1;     /* planner will set aggno and aggtransno */
	aggref->aggtransno = -1;
	aggref->location = -1;
	aggref->aggargtypes = argTypes;

	bool aggDistinct = false;
	parseState->p_hasAggs = true;
	transformAggregateCall(parseState, aggref, args, NIL, aggDistinct);
	return aggref;
}


/*
 * Handles the $count stage. Injects an aggregate of
 * bsonsum('{ "": 1 }');
 * First moves existing query to a subquery.
 * Then injects the aggregate projector.
 * We request a new subquery for subsequent stages, but it
 * may not be needed.
 */
static Query *
HandleCount(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_COUNT);
	StringView countField = { 0 };
	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		countField.string = existingValue->value.v_utf8.str;
		countField.length = existingValue->value.v_utf8.len;
	}

	if (countField.length == 0)
	{
		ereport(ERROR, (errcode(MongoLocation40156),
						errmsg("the count field must be a non-empty string")));
	}

	if (StringViewStartsWith(&countField, '$'))
	{
		ereport(ERROR, (errcode(MongoLocation40158),
						errmsg("the count field cannot be a $-prefixed path")));
	}

	if (StringViewContains(&countField, '.'))
	{
		ereport(ERROR, (errcode(MongoLocation40160),
						errmsg("the count field cannot contain '.'")));
	}

	/* Count requires the existing query to move to subquery */
	query = MigrateQueryToSubQuery(query, context);

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(query->targetList);


	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_SELECT_TARGET;
	parseState->p_next_resno = firstEntry->resno + 1;

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt32(&writer, "", 0, 1);
	Expr *constValue = (Expr *) MakeBsonConst(PgbsonWriterGetPgbson(&writer));
	Aggref *aggref = CreateSingleArgAggregate(BsonSumAggregateFunctionOid(), constValue,
											  parseState);
	pfree(parseState);

	query->hasAggs = true;

	/* We wrap the count in a bson_repath_and_build */
	Const *countFieldText = MakeTextConst(countField.string, countField.length);

	List *args = list_make2(countFieldText, aggref);
	FuncExpr *expression = makeFuncExpr(BsonRepathAndBuildFunctionOid(), BsonTypeId(),
										args, InvalidOid, InvalidOid,
										COERCE_EXPLICIT_CALL);
	firstEntry->expr = (Expr *) expression;

	/* Having count means the next stage must be a new outer query */
	context->requiresSubQuery = true;
	return query;
}


/*
 * Handles the $replaceWith stage. Converts to a $replaceRoot stage
 * and calls $replaceRoot
 */
static Query *
HandleReplaceWith(const bson_value_t *existingValue, Query *query,
				  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_REPLACE_WITH);

	/* Convert to replaceRoot */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendValue(&writer, "newRoot", 7, existingValue);
	pgbson *bson = PgbsonWriterGetPgbson(&writer);
	bson_value_t currentValue = ConvertPgbsonToBsonValue(bson);
	return HandleReplaceRoot(&currentValue, query, context);
}


/*
 * Handles the $sort stage.
 * Creates a subquery if there's a skip/limit (Since those need to be
 * applied first).
 * Parses the sort spec and takes the current projector and applies the sort
 * function on it. Injects the tle_sort_group similiar to the parser phase.
 */
static Query *
HandleSort(const bson_value_t *existingValue, Query *query,
		   AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_SORT);
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Expected document for sort specification")));
	}

	/* If there's an existing skip/limit we need to push those down */
	if (query->limitOffset != NULL || query->limitCount != NULL ||
		query->sortClause != NULL)
	{
		query = MigrateQueryToSubQuery(query, context);
	}

	/* Store the sort in the context (used for downstream groups) */
	context->sortSpec = *existingValue;

	bson_iter_t sortSpec;
	BsonValueInitIterator(existingValue, &sortSpec);

	/* Take the current output (That's to be sorted)*/
	TargetEntry *entry = linitial(query->targetList);

	/* Grab a copy of the targetList */
	List *targetEntryList = list_copy(query->targetList);


	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_ORDER_BY;

	/* set after what is already taken */
	parseState->p_next_resno = list_length(query->targetList) + 1;
	List *sortlist = NIL;
	bool isNaturalSort = false;
	while (bson_iter_next(&sortSpec))
	{
		pgbsonelement element;
		BsonIterToPgbsonElement(&sortSpec, &element);

		if (strcmp(element.path, "$natural") == 0)
		{
			isNaturalSort = true;
			break;
		}

		Expr *sortInput = entry->expr;
		pgbsonelement subOrderingElement;
		if (element.bsonValue.value_type == BSON_TYPE_DOCUMENT &&
			TryGetBsonValueToPgbsonElement(&element.bsonValue, &subOrderingElement) &&
			subOrderingElement.pathLength == 5 &&
			strncmp(subOrderingElement.path, "$meta", 5) == 0)
		{
			RangeTblEntry *rte = linitial(query->rtable);
			if (rte->rtekind == RTE_RELATION ||
				rte->rtekind == RTE_FUNCTION)
			{
				/* This is a base table */
				sortInput = (Expr *) MakeSimpleDocumentVar();
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("Invalid sort direction %s",
									   BsonValueToJsonForLogging(
										   &element.bsonValue))));
			}
		}

		pgbson *sortDoc = PgbsonElementToPgbson(&element);
		Const *sortBson = MakeBsonConst(sortDoc);
		bool isAscending = ValidateOrderbyExpressionAndGetIsAscending(sortDoc);
		Expr *expr = (Expr *) makeFuncExpr(BsonOrderByFunctionOid(),
										   BsonTypeId(),
										   list_make2(sortInput, sortBson),
										   InvalidOid, InvalidOid,
										   COERCE_EXPLICIT_CALL);
		SortBy *sortBy = makeNode(SortBy);
		sortBy->location = -1;
		sortBy->sortby_dir = isAscending ? SORTBY_ASC : SORTBY_DESC;
		sortBy->sortby_nulls = isAscending ? SORTBY_NULLS_FIRST : SORTBY_NULLS_LAST;
		sortBy->node = (Node *) expr;

		bool resjunk = true;
		TargetEntry *sortEntry = makeTargetEntry((Expr *) expr,
												 (AttrNumber) parseState->p_next_resno++,
												 "?sort?",
												 resjunk);
		targetEntryList = lappend(targetEntryList, sortEntry);
		sortlist = addTargetToSortList(parseState, sortEntry,
									   sortlist, targetEntryList, sortBy);
	}

	pfree(parseState);
	if (isNaturalSort)
	{
		/* $natural sort overrides everything */
		return query;
	}

	if (sortlist == NIL)
	{
		ereport(ERROR, (errcode(MongoLocation15976),
						errmsg("$sort stage must have at least one sort key")));
	}

	query->targetList = targetEntryList;
	query->sortClause = sortlist;

	return query;
}


/*
 * Handles the $sortByCount stage.
 * Creates a group followed by a sort and calls each handler
 */
static Query *
HandleSortByCount(const bson_value_t *existingValue, Query *query,
				  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_SORT_BY_COUNT);

	/* Do validations */
	bool isInvalidSpec = false;
	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		isInvalidSpec = (existingValue->value.v_utf8.len == 0 ||
						 existingValue->value.v_utf8.str[0] != '$');
	}
	else if (existingValue->value_type == BSON_TYPE_DOCUMENT)
	{
		pgbsonelement expressionElement;
		isInvalidSpec =
			!TryGetBsonValueToPgbsonElement(existingValue, &expressionElement) ||
			expressionElement.pathLength == 0 ||
			expressionElement.path[0] != '$';
	}
	else
	{
		isInvalidSpec = true;
	}

	if (isInvalidSpec)
	{
		ereport(ERROR, (errcode(MongoLocation40147),
						errmsg(
							"the sortByCount field must be defined as a $-prefixed path or an expression inside an object")));
	}

	/* Convert to
	 * { $group: { _id: <expression>, count: { $sum: 1 } } },
	 * { $sort: { count: -1 } }
	 */

	pgbson_writer groupWriter;
	PgbsonWriterInit(&groupWriter);
	PgbsonWriterAppendValue(&groupWriter, "_id", 3, existingValue);

	pgbson_writer countWriter;
	PgbsonWriterStartDocument(&groupWriter, "count", 5, &countWriter);
	PgbsonWriterAppendInt32(&countWriter, "$sum", 4, 1);
	PgbsonWriterEndDocument(&groupWriter, &countWriter);

	pgbson *group = PgbsonWriterGetPgbson(&groupWriter);
	bson_value_t groupValue = ConvertPgbsonToBsonValue(group);

	query = HandleGroup(&groupValue, query, context);
	if (context->requiresSubQuery)
	{
		query = MigrateQueryToSubQuery(query, context);
	}

	/* Add sort */
	pgbson_writer sortWriter;
	PgbsonWriterInit(&sortWriter);
	PgbsonWriterAppendInt32(&sortWriter, "count", 5, -1);
	pgbson *sort = PgbsonWriterGetPgbson(&sortWriter);
	bson_value_t sortValue = ConvertPgbsonToBsonValue(sort);

	query = HandleSort(&sortValue, query, context);
	return query;
}


/*
 * Helper method that adds a group expression projection to the query's targetList.
 * Creates a VAR that can be used in the projector of the higher level sub-query.
 * Used with GROUP to ensure that we can split the group into
 * generate projectors/generate bson_repath_and_build in the second stage.
 */
inline static Var *
AddGroupExpression(Expr *expression, ParseState *parseState, char *identifiers,
				   Query *query, Oid outputOid, TargetEntry **createdEntry)
{
	int identifierInt = parseState->p_next_resno++;
	identifiers[0] = 'c';
	pg_ltoa(identifierInt, &identifiers[1]);
	bool resjunk = false;
	TargetEntry *entry = makeTargetEntry(expression, identifierInt, pstrdup(identifiers),
										 resjunk);
	query->targetList = lappend(query->targetList, entry);

	if (createdEntry)
	{
		*createdEntry = entry;
	}

	query->hasAggs = true;
	parseState->p_hasAggs = true;
	Index childIndex = 1;
	return makeVar(childIndex, entry->resno, outputOid, -1, InvalidOid, 0);
}


/**
 * Parses the input document for FirstN and LastN group expression operator and extracts the value for input and n.
 * @param inputDocument: input document for the $firstN operator
 * @param input:  this is a pointer which after parsing will hold array expression
 * @param elementsToFetch: this is a pointer which after parsing will hold n i.e. how many elements to fetch for result
 * @param opName: this contains the name of the operator for error msg formatting purposes. This value is supposed to be $firstN/$lastN.
 */
static void
ParseInputDocumentForFirstAndLastN(const bson_value_t *inputDocument, bson_value_t *input,
								   bson_value_t *elementsToFetch, const char *opName)
{
	if (inputDocument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation5787801), errmsg(
							"specification must be an object; found %s :%s",
							opName, BsonValueToJsonForLogging(inputDocument)),
						errhint(
							"specification must be an object; opname: %s type found :%s",
							opName, BsonTypeName(inputDocument->value_type))));
	}
	bson_iter_t docIter;
	BsonValueInitIterator(inputDocument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			*input = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "n") == 0)
		{
			*elementsToFetch = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation5787901), errmsg(
								"%s found an unknown argument: %s", opName, key),
							errhint(
								"%s found an unknown argument", opName)));
		}
	}

	/**
	 * Validation check to see if input and elements to fetch are present otherwise throw error.
	 */
	if (input->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5787907), errmsg(
							"%s requires an 'input' field", opName),
						errhint(
							"%s requires an 'input' field", opName)));
	}

	if (elementsToFetch->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation5787906), errmsg(
							"%s requires an 'n' field", opName),
						errhint(
							"%s requires an 'n' field", opName)));
	}
}


/**
 * This function validates and throws error in case bson type is not a numeric > 0 and less than max value of int64 i.e. 9223372036854775807
 */
static void
ValidateElementForFirstAndLastN(bson_value_t *elementsToFetch, const
								char *opName)
{
	switch (elementsToFetch->value_type)
	{
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			if (!IsBsonValueFixedInteger(elementsToFetch))
			{
				ereport(ERROR, (errcode(MongoLocation5787903), errmsg(
									"Value for 'n' must be of integral type, but found %s",
									BsonValueToJsonForLogging(elementsToFetch)),
								errhint(
									"Value for 'n' must be of integral type, but found of type %s",
									BsonTypeName(elementsToFetch->value_type))));
			}

			/* This is done as elements to fetch must only be int64. */
			bool throwIfFailed = true;
			elementsToFetch->value.v_int64 = BsonValueAsInt64WithRoundingMode(
				elementsToFetch, ConversionRoundingMode_Floor, throwIfFailed);
			elementsToFetch->value_type = BSON_TYPE_INT64;

			if (elementsToFetch->value.v_int64 <= 0)
			{
				ereport(ERROR, (errcode(MongoLocation5787908), errmsg(
									"'n' must be greater than 0, found %s",
									BsonValueToJsonForLogging(elementsToFetch)),
								errhint(
									"'n' must be greater than 0, found %ld",
									elementsToFetch->value.v_int64)));
			}
			if (elementsToFetch->value.v_int64 > 10)
			{
				if (strncmp(opName, "$lastN", 6) == 0)
				{
					ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_LASTN_GT10);
				}
				else
				{
					ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_FIRSTN_GT10);
				}
			}
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoLocation5787902), errmsg(
								"Value for 'n' must be of integral type, but found %s",
								BsonValueToJsonForLogging(elementsToFetch)),
							errhint(
								"Value for 'n' must be of integral type, but found of type %s",
								BsonTypeName(elementsToFetch->value_type))));
		}
	}
}


/*
 * Simple helper method that has logic to insert a Group accumulator to a query.
 * This adds the group aggregate to the TargetEntry (for projection)
 * and also adds the necessary data to the bson_repath_and_build arguments.
 */
inline static List *
AddSimpleGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
						  List *repathArgs, Const *accumulatorText,
						  ParseState *parseState, char *identifiers,
						  Expr *documentExpr, Oid aggregateFunctionOid)
{
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  accumulatorValue));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs = list_make3(documentExpr, constValue, trueConst);
	FuncExpr *accumFunc = makeFuncExpr(
		BsonExpressionGetFunctionOid(), BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);
	Aggref *aggref = CreateSingleArgAggregate(aggregateFunctionOid,
											  (Expr *) accumFunc, parseState);
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers,
														query, TEXTOID, NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) aggref,
														parseState, identifiers,
														query, BsonTypeId(),
														NULL));
	return repathArgs;
}


/*
 * Simple helper method that has logic to insert a BSON_ARRAY_AGG accumulator to a query.
 * This adds the group aggregate to the TargetEntry (for projection)
 * and also adds the necessary data to the bson_repath_and_build arguments.
 */
inline static List *
AddArrayAggGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
							List *repathArgs, Const *accumulatorText,
							ParseState *parseState, char *identifiers,
							Expr *documentExpr, Oid aggregateFunctionOid,
							char *fieldPath, bool handleSingleValue)
{
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  accumulatorValue));
	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *funcArgs = list_make3(documentExpr, constValue, trueConst);

	FuncExpr *accumFunc = makeFuncExpr(
		BsonExpressionGetFunctionOid(), BsonTypeId(), funcArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	List *aggregateArgs = list_make3(
		(Expr *) accumFunc,
		MakeTextConst(fieldPath, strlen(fieldPath)),
		MakeBoolValueConst(handleSingleValue));
	List *argTypesList = list_make3_oid(BsonTypeId(), TEXTOID, BOOLOID);

	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid,
											 aggregateArgs,
											 argTypesList,
											 parseState);

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers,
														query, TEXTOID, NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) aggref,
														parseState, identifiers,
														query, BsonTypeId(),
														NULL));
	return repathArgs;
}


/*
 * Simple helper method that has logic to insert a BSON_MERGE_OBJECTS accumulator to a query.
 * This adds the group aggregate to the TargetEntry (for projection)
 * and also adds the necessary data to the bson_repath_and_build arguments.
 */
inline static List *
AddMergeObjectsGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
								List *repathArgs, Const *accumulatorText,
								ParseState *parseState, char *identifiers,
								Expr *documentExpr, Oid aggregateFunctionOid,
								const bson_value_t *sortSpec)
{
	/* First apply the sorted aggs */
	int nelems = BsonDocumentValueCountKeys(sortSpec);
	Datum *sortDatumArray = palloc(sizeof(Datum) * nelems);

	bson_iter_t sortIter;
	BsonValueInitIterator(sortSpec, &sortIter);
	int i = 0;
	while (bson_iter_next(&sortIter))
	{
		pgbsonelement sortElement = { 0 };
		sortElement.path = bson_iter_key(&sortIter);
		sortElement.pathLength = strlen(sortElement.path);
		sortElement.bsonValue = *bson_iter_value(&sortIter);
		sortDatumArray[i] = PointerGetDatum(PgbsonElementToPgbson(&sortElement));
		i++;
	}

	/* Here we use INT_MAX to sort the whole array regardless of its length. */
	Const *nConst = makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
							  Int64GetDatum(INT_MAX), false, true);
	ArrayType *arrayValue = construct_array(sortDatumArray, nelems, BsonTypeId(), -1,
											false, TYPALIGN_INT);

	Const *sortArrayConst = makeConst(get_array_type(BsonTypeId()), -1, InvalidOid, -1,
									  PointerGetDatum(arrayValue), false, false);

	/*
	 * Here we add a parameter with the input expression. The reason is we need
	 * to evaluate it against the document after the sort takes place.
	 */
	Expr *inputExpression = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
													   accumulatorValue));
	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid,
											 list_make4(documentExpr, nConst,
														sortArrayConst,
														inputExpression),
											 list_make4_oid(BsonTypeId(),
															nConst->consttype,
															sortArrayConst->consttype,
															BsonTypeId()),
											 parseState);

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers,
														query, TEXTOID, NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) aggref,
														parseState, identifiers,
														query, BsonTypeId(),
														NULL));
	return repathArgs;
}


/*
 * Simple helper method that has logic to insert a Group accumulator to a query.
 * This adds the group aggregate to the TargetEntry (for projection)
 * and also adds the necessary data to the bson_expression_map arguments.
 */
inline static List *
AddSimpleNGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
						   List *repathArgs, Const *accumulatorText,
						   ParseState *parseState, char *identifiers,
						   Expr *documentExpr, Oid aggregateFunctionOid,
						   StringView *accumulatorName)
{
	/* Parse accumulatorValue to pull out input/N */
	bson_value_t input = { 0 };
	bson_value_t elementsToFetch = { 0 };
	ParseInputDocumentForFirstAndLastN(accumulatorValue, &input, &elementsToFetch,
									   accumulatorName->string);
	ValidateElementForFirstAndLastN(&elementsToFetch, accumulatorName->string);

	/* First add the agg function */
	Const *nConst = makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
							  Int64GetDatum(elementsToFetch.value.v_int64), false, true);
	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid,
											 list_make2((Expr *) documentExpr, nConst),
											 list_make2_oid(BsonTypeId(),
															nConst->consttype),
											 parseState);

	/* First apply the map function to the document */
	Const *fieldConst = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(""),
								  false,
								  false);
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  &input));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs = list_make4(aggref, fieldConst, constValue, trueConst);
	FuncExpr *accumFunc = makeFuncExpr(
		BsonExpressionMapFunctionOid(), BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers,
														query, TEXTOID,
														NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumFunc,
														parseState, identifiers,
														query, BsonTypeId(), NULL));
	return repathArgs;
}


/*
 * Add a sorted group accumulator e.g. BSONFIRST/BSONLAST
 */
inline static List *
AddSortedGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
						  List *repathArgs, Const *accumulatorText,
						  ParseState *parseState, char *identifiers,
						  Expr *documentExpr, Oid aggregateFunctionOid,
						  const bson_value_t *sortSpec)
{
	/* First apply the sorted agg (FIRST/LAST) */
	int nelems = BsonDocumentValueCountKeys(sortSpec);
	Datum *sortDatumArray = palloc(sizeof(Datum) * nelems);

	bson_iter_t sortIter;
	BsonValueInitIterator(sortSpec, &sortIter);
	int i = 0;
	while (bson_iter_next(&sortIter))
	{
		pgbsonelement sortElement = { 0 };
		sortElement.path = bson_iter_key(&sortIter);
		sortElement.pathLength = strlen(sortElement.path);
		sortElement.bsonValue = *bson_iter_value(&sortIter);
		sortDatumArray[i] = PointerGetDatum(PgbsonElementToPgbson(&sortElement));
		i++;
	}

	ArrayType *arrayValue = construct_array(sortDatumArray, nelems, BsonTypeId(), -1,
											false, TYPALIGN_INT);

	Const *sortArrayConst = makeConst(get_array_type(BsonTypeId()), -1, InvalidOid, -1,
									  PointerGetDatum(arrayValue), false, false);
	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid,
											 list_make2(documentExpr, sortArrayConst),
											 list_make2_oid(BsonTypeId(),
															sortArrayConst->consttype),
											 parseState);

	/* Now apply the bson_expression_get */
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  accumulatorValue));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs = list_make3(aggref, constValue, trueConst);

	FuncExpr *accumFunc = makeFuncExpr(
		BsonExpressionGetFunctionOid(), BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers,
														query, TEXTOID, NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumFunc,
														parseState, identifiers,
														query, BsonTypeId(),
														NULL));
	return repathArgs;
}


/*
 * Add a sorted group accumulator e.g. BSONFIRSTN/BSONLASTN
 */
inline static List *
AddSortedNGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
						   List *repathArgs, Const *accumulatorText,
						   ParseState *parseState, char *identifiers,
						   Expr *documentExpr, Oid aggregateFunctionOid,
						   const bson_value_t *sortSpec, StringView *accumulatorName)
{
	/* Parse accumulatorValue to pull out input/N*/
	bson_value_t input = { 0 };
	bson_value_t elementsToFetch = { 0 };
	ParseInputDocumentForFirstAndLastN(accumulatorValue, &input, &elementsToFetch,
									   accumulatorName->string);
	ValidateElementForFirstAndLastN(&elementsToFetch, accumulatorName->string);

	/* First apply the sorted agg (FIRSTN/LASTN) */
	int nelems = BsonDocumentValueCountKeys(sortSpec);
	Datum *sortDatumArray = palloc(sizeof(Datum) * nelems);

	bson_iter_t sortIter;
	BsonValueInitIterator(sortSpec, &sortIter);
	int i = 0;
	while (bson_iter_next(&sortIter))
	{
		pgbsonelement sortElement = { 0 };
		sortElement.path = bson_iter_key(&sortIter);
		sortElement.pathLength = strlen(sortElement.path);
		sortElement.bsonValue = *bson_iter_value(&sortIter);
		sortDatumArray[i] = PointerGetDatum(PgbsonElementToPgbson(&sortElement));
		i++;
	}

	Const *nConst = makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
							  Int64GetDatum(elementsToFetch.value.v_int64), false, true);
	ArrayType *arrayValue = construct_array(sortDatumArray, nelems, BsonTypeId(), -1,
											false, TYPALIGN_INT);
	Const *sortArrayConst = makeConst(get_array_type(BsonTypeId()), -1, InvalidOid, -1,
									  PointerGetDatum(arrayValue), false, false);
	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid,
											 list_make3(documentExpr, nConst,
														sortArrayConst),
											 list_make3_oid(BsonTypeId(),
															nConst->consttype,
															sortArrayConst->consttype),
											 parseState);

	/* Now apply the bson_expression_map */
	Const *fieldConst = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(""),
								  false,
								  false);
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  &input));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs = list_make4(aggref, fieldConst, constValue, trueConst);

	FuncExpr *accumFunc = makeFuncExpr(
		BsonExpressionMapFunctionOid(), BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers,
														query, TEXTOID, NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumFunc,
														parseState, identifiers,
														query, BsonTypeId(),
														NULL));
	return repathArgs;
}


/*
 * Handles the $group stage.
 * Creates a subquery.
 * Then creates a grouping specified by the _id expression.
 * Then creates accumulators in the projectors.
 *
 * The raw accumulators/group are in the first query
 * The aggregates are then pushed to a subquery and then an outer query
 * with the bson_repath_and_build is added as part of the group.
 * This is done because without this, sharded multi-node group by fails
 * due to a quirk in citus's worker query generation.
 */
Query *
HandleGroup(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_GROUP);

	/* Part 1, let's do the group */
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation15947),
						errmsg("a group's fields must be specified in an object")));
	}

	/* Push prior stuff to a subquery first since we're gonna aggregate our way */
	query = MigrateQueryToSubQuery(query, context);

	/* Take the current output (That's to be grouped)*/
	TargetEntry *origEntry = linitial(query->targetList);

	/* Clear the group's output */
	query->targetList = NIL;

	bson_iter_t groupIter;
	BsonValueInitIterator(existingValue, &groupIter);

	/* First get the _id */
	bson_value_t idValue = { 0 };
	while (bson_iter_next(&groupIter))
	{
		StringView keyView = bson_iter_key_string_view(&groupIter);
		if (StringViewEquals(&keyView, &IdFieldStringView))
		{
			idValue = *bson_iter_value(&groupIter);
			break;
		}
	}

	if (idValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation15955),
						errmsg("a group specification must include an _id")));
	}

	pgbson *groupValue = BsonValueToDocumentPgbson(&idValue);


	ParseState *parseState = make_parsestate(NULL);
	parseState->p_next_resno = 1;
	parseState->p_expr_kind = EXPR_KIND_GROUP_BY;

	List *groupArgs = list_make3(origEntry->expr, MakeBsonConst(groupValue),
								 MakeBoolValueConst(true));
	FuncExpr *groupFunc = makeFuncExpr(
		BsonExpressionGetFunctionOid(), BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	/* Now do the projector / accumulators
	 * We do this in 2 stages to handle citus query generation.
	 * In the first phase we just project out the group and accumulators
	 * Identifiers are basically 'c' + some number.
	 * We set aside characters to "tostring" the number such that
	 * (c + <number> + \0). Note this is a temporary buffer - we
	 * pstrdup it after creating it.
	 */
	char identifiers[UINT32_MAX_STR_LEN + 2];

	Const *idFieldText = MakeTextConst("_id", 3);

	List *repathArgs = NIL;
	TargetEntry *groupEntry;
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) idFieldText, parseState,
														identifiers, query, TEXTOID,
														NULL));
	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) groupFunc, parseState,
														identifiers, query, BsonTypeId(),
														&groupEntry));

	/* Now add accumulators */
	parseState->p_expr_kind = EXPR_KIND_SELECT_TARGET;
	BsonValueInitIterator(existingValue, &groupIter);
	while (bson_iter_next(&groupIter))
	{
		StringView keyView = bson_iter_key_string_view(&groupIter);
		if (StringViewEquals(&keyView, &IdFieldStringView))
		{
			continue;
		}

		Const *accumulatorText = MakeTextConst(keyView.string, keyView.length);

		bson_iter_t accumulatorIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&groupIter) ||
			!bson_iter_recurse(&groupIter, &accumulatorIterator))
		{
			ereport(ERROR, (errcode(MongoLocation40234),
							errmsg("The field '%.*s' must be an accumulator object",
								   keyView.length, keyView.string)));
		}

		pgbsonelement accumulatorElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&accumulatorIterator,
													   &accumulatorElement))
		{
			ereport(ERROR, (errcode(MongoLocation40238),
							errmsg("The field '%.*s' must specify one accumulator",
								   keyView.length, keyView.string)));
		}

		StringView accumulatorName = {
			.length = accumulatorElement.pathLength, .string = accumulatorElement.path
		};
		if (StringViewEqualsCString(&accumulatorName, "$avg"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonAvgAggregateFunctionOid());
		}
		else if (StringViewEqualsCString(&accumulatorName, "$sum"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonSumAggregateFunctionOid());
		}
		else if (StringViewEqualsCString(&accumulatorName, "$max"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonMaxAggregateFunctionOid());
		}
		else if (StringViewEqualsCString(&accumulatorName, "$min"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonMinAggregateFunctionOid());
		}
		else if (StringViewEqualsCString(&accumulatorName, "$count"))
		{
			bson_value_t countValue = { 0 };
			countValue.value_type = BSON_TYPE_INT32;
			countValue.value.v_int32 = 1;

			repathArgs = AddSimpleGroupAccumulator(query, &countValue, repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonSumAggregateFunctionOid());
		}
		else if (StringViewEqualsCString(&accumulatorName, "$first"))
		{
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleGroupAccumulator(query,
													   &accumulatorElement.bsonValue,
													   repathArgs,
													   accumulatorText, parseState,
													   identifiers,
													   origEntry->expr,
													   BsonFirstOnSortedAggregateFunctionOid());
			}
			else
			{
				repathArgs = AddSortedGroupAccumulator(query,
													   &accumulatorElement.bsonValue,
													   repathArgs,
													   accumulatorText, parseState,
													   identifiers,
													   origEntry->expr,
													   BsonFirstAggregateFunctionOid(),
													   &context->sortSpec);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$last"))
		{
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleGroupAccumulator(query,
													   &accumulatorElement.bsonValue,
													   repathArgs,
													   accumulatorText, parseState,
													   identifiers,
													   origEntry->expr,
													   BsonLastOnSortedAggregateFunctionOid());
			}
			else
			{
				repathArgs = AddSortedGroupAccumulator(query,
													   &accumulatorElement.bsonValue,
													   repathArgs,
													   accumulatorText, parseState,
													   identifiers,
													   origEntry->expr,
													   BsonLastAggregateFunctionOid(),
													   &context->sortSpec);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$firstN"))
		{
			ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_FIRSTN);
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleNGroupAccumulator(query,
														&accumulatorElement.bsonValue,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonFirstNOnSortedAggregateFunctionOid(),
														&accumulatorName);
			}
			else
			{
				repathArgs = AddSortedNGroupAccumulator(query,
														&accumulatorElement.bsonValue,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonFirstNAggregateFunctionOid(),
														&context->sortSpec,
														&accumulatorName);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$lastN"))
		{
			ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_LASTN);
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleNGroupAccumulator(query,
														&accumulatorElement.bsonValue,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonLastNOnSortedAggregateFunctionOid(),
														&accumulatorName);
			}
			else
			{
				repathArgs = AddSortedNGroupAccumulator(query,
														&accumulatorElement.bsonValue,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonLastNAggregateFunctionOid(),
														&context->sortSpec,
														&accumulatorName);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$addToSet"))
		{
			repathArgs = AddSimpleGroupAccumulator(query,
												   &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonAddToSetAggregateFunctionOid());
		}
		else if (StringViewEqualsCString(&accumulatorName, "$mergeObjects"))
		{
			if (EnableGroupMergeObjectsSupport || IsClusterVersionAtleastThis(1, 18, 0))
			{
				if (context->sortSpec.value_type == BSON_TYPE_EOD)
				{
					repathArgs = AddSimpleGroupAccumulator(query,
														   &accumulatorElement.bsonValue,
														   repathArgs,
														   accumulatorText, parseState,
														   identifiers,
														   origEntry->expr,
														   BsonMergeObjectsOnSortedFunctionOid());
				}
				else
				{
					repathArgs = AddMergeObjectsGroupAccumulator(query,
																 &accumulatorElement.
																 bsonValue,
																 repathArgs,
																 accumulatorText,
																 parseState,
																 identifiers,
																 origEntry->expr,
																 BsonMergeObjectsFunctionOid(),
																 &context->sortSpec);
				}
			}
			else
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg("Accumulator $mergeObjects not implemented yet")));
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$push"))
		{
			char *fieldPath = "";
			bool handleSingleValue = true;
			repathArgs = AddArrayAggGroupAccumulator(query,
													 &accumulatorElement.bsonValue,
													 repathArgs,
													 accumulatorText, parseState,
													 identifiers,
													 origEntry->expr,
													 BsonArrayAggregateAllArgsFunctionOid(),
													 fieldPath,
													 handleSingleValue);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$stdDevSamp"))
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Accumulator $stdDevSamp not implemented yet")));
		}
		else if (StringViewEqualsCString(&accumulatorName, "$stdDevPop"))
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Accumulator $stdDevPop not implemented yet")));
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation15952),
							errmsg("Unknown group operator %s",
								   accumulatorElement.path),
							errhint("Unknown group operator %s",
									accumulatorElement.path)));
		}
	}

	/* Assign the _id clause as what we're grouping on */
	SortGroupClause *grpcl = makeNode(SortGroupClause);
	grpcl->tleSortGroupRef = assignSortGroupRef(groupEntry, query->targetList);
	grpcl->eqop = BsonEqualOperatorId();
	grpcl->sortop = BsonLessThanOperatorId();
	grpcl->nulls_first = false; /* OK with or without sortop */
	grpcl->hashable = false;
	query->groupClause = list_make1(grpcl);

	/* Now that the group + accumulators are done, push to a subquery
	 * Request preserving the N-entry T-list
	 */
	context->expandTargetList = true;
	query = MigrateQueryToSubQuery(query, context);

	/* Take the output and replace it with the repath_and_build */
	TargetEntry *entry = linitial(query->targetList);
	FuncExpr *repathExpression = makeFuncExpr(BsonRepathAndBuildFunctionOid(),
											  BsonTypeId(), repathArgs, InvalidOid,
											  InvalidOid, COERCE_EXPLICIT_CALL);
	entry->expr = (Expr *) repathExpression;
	entry->resname = origEntry->resname;


	/* Mark new stages to push a new subquery */
	context->requiresSubQuery = true;
	return query;
}


/*
 * Pushes the current query into a subquery.
 * Then creates a brand new query that projects the 'document' value
 * from the underlying subquery.
 */
Query *
MigrateQueryToSubQuery(Query *parse, AggregationPipelineBuildContext *context)
{
	context->numNestedLevels++;

	TargetEntry *tle = (TargetEntry *) linitial(parse->targetList);
	RangeTblEntry *rte = MakeSubQueryRte(parse, context->stageNum,
										 context->nestedPipelineLevel,
										 "agg", context->expandTargetList);
	context->expandTargetList = false;

	Index childIndex = 1;
	Var *newQueryOutput = makeVar(childIndex, tle->resno, BsonTypeId(), -1, InvalidOid,
								  0);
	TargetEntry *upperEntry = makeTargetEntry((Expr *) newQueryOutput, 1, tle->resname,
											  tle->resjunk);


	Query *newquery = makeNode(Query);
	newquery->commandType = CMD_SELECT;
	newquery->querySource = parse->querySource;
	newquery->canSetTag = true;
	newquery->targetList = list_make1(upperEntry);
	newquery->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	newquery->jointree = makeFromExpr(list_make1(rtr), NULL);

	context->requiresSubQuery = false;
	return newquery;
}


/*
 * Creates an RTE for a Subquery provided given the prefixed stage name
 */
RangeTblEntry *
MakeSubQueryRte(Query *subQuery, int stageNum, int pipelineDepth,
				const char *prefix, bool includeAllColumns)
{
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_SUBQUERY;
	rte->subquery = subQuery;
	rte->lateral = false;
	rte->inh = false;
	rte->inFromCl = true;

	StringInfo s = makeStringInfo();
	if (pipelineDepth > 0)
	{
		appendStringInfo(s, "%s_stage_sub_%d_%d", prefix, pipelineDepth, stageNum);
	}
	else
	{
		appendStringInfo(s, "%s_stage_%d", prefix, stageNum);
	}

	rte->alias = makeAlias(s->data, NIL);
	if (includeAllColumns)
	{
		ListCell *cell;
		List *colnames = NIL;
		foreach(cell, subQuery->targetList)
		{
			TargetEntry *tle = lfirst(cell);
			colnames = lappend(colnames, makeString(tle->resname ? tle->resname : ""));
		}

		rte->eref = makeAlias(s->data, colnames);
	}
	else
	{
		TargetEntry *tle = linitial(subQuery->targetList);
		List *colnames = list_make1(makeString(tle->resname ? tle->resname : ""));
		rte->eref = makeAlias(s->data, colnames);
	}

	return rte;
}


/*
 * A simple function that never requires persistent cursors
 */
static bool
RequiresPersistentCursorFalse(const bson_value_t *pipelineValue)
{
	return false;
}


/*
 * A simple function that always requires persistent cursors
 */
static bool
RequiresPersistentCursorTrue(const bson_value_t *pipelineValue)
{
	return true;
}


/*
 * Checks that the limit stage requires persistence (true if it's not limit 1).
 */
static bool
RequiresPersistentCursorLimit(const bson_value_t *pipelineValue)
{
	if (pipelineValue->value_type != BSON_TYPE_EOD &&
		BsonValueIsNumber(pipelineValue))
	{
		int32_t limit = BsonValueAsInt32(pipelineValue);
		return limit != 1 && limit != 0;
	}

	return pipelineValue->value_type != BSON_TYPE_EOD;
}


/*
 * Checks that the skip stage requires persistence (true if it's not skip 0).
 */
static bool
RequiresPersistentCursorSkip(const bson_value_t *pipelineValue)
{
	if (pipelineValue->value_type != BSON_TYPE_EOD &&
		BsonValueIsNumber(pipelineValue))
	{
		int32_t skip = BsonValueAsInt32(pipelineValue);
		return skip != 0;
	}

	return pipelineValue->value_type != BSON_TYPE_EOD;
}


/*
 * Given a database, and a view definition, recursively extracts the
 * views pointed to by the view definition until a base collection (or non-existent)
 * collection is found. Also appends the pipeline stages associated with it to the
 * List of pipeline stages.
 */
static MongoCollection *
ExtractViewDefinitionAndPipeline(Datum databaseDatum, pgbson *viewDefinition,
								 List **pipelineStages)
{
	int viewDepth = 0;
	while (true)
	{
		CHECK_FOR_INTERRUPTS();
		check_stack_depth();

		if (viewDepth > MAX_VIEW_DEPTH)
		{
			ereport(ERROR, (errcode(MongoViewDepthLimitExceeded),
							errmsg("View depth exceeded limit %d", MAX_VIEW_DEPTH)));
		}

		viewDepth++;
		ViewDefinition definition = { 0 };
		DecomposeViewDefinition(viewDefinition, &definition);

		if (definition.pipeline.value_type != BSON_TYPE_EOD)
		{
			bson_value_t *valueCopy = palloc(sizeof(bson_value_t));
			*valueCopy = definition.pipeline;
			*pipelineStages = lappend(*pipelineStages, valueCopy);
		}

		MongoCollection *collection = GetMongoCollectionOrViewByNameDatum(
			databaseDatum, CStringGetTextDatum(definition.viewSource), NoLock);
		if (collection == NULL)
		{
			/* It's a non-existent table - we can stop the search */
			return collection;
		}
		else if (collection->viewDefinition == NULL)
		{
			/* It's a base table - lock the table and stop the search */
			GetRelationIdForCollectionId(collection->collectionId, AccessShareLock);
			return collection;
		}
		else
		{
			/* It's a view redo the check */
			viewDefinition = collection->viewDefinition;
		}
	}
}


/*
 * Updates the base table
 */
Query *
GenerateBaseTableQuery(Datum databaseDatum, const StringView *collectionNameView,
					   pg_uuid_t *collectionUuid,
					   AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	context->collectionNameView = *collectionNameView;
	context->namespaceName = CreateNamespaceName(DatumGetTextPP(databaseDatum),
												 collectionNameView);
	Datum collectionNameDatum = PointerGetDatum(
		cstring_to_text_with_len(collectionNameView->string, collectionNameView->length));

	MongoCollection *collection = context->allowShardBaseTable ?
								  GetMongoCollectionOrViewByNameDatumWithLocalShard(
		databaseDatum, collectionNameDatum, AccessShareLock) :
								  GetMongoCollectionOrViewByNameDatum(
		databaseDatum, collectionNameDatum, AccessShareLock);

	/* CollectionUUID mismatch when collection doesn't exist */
	if (collectionUuid != NULL)
	{
		if (collection == NULL)
		{
			ereport(ERROR, (errcode(MongoCollectionUUIDMismatch),
							errmsg(
								"Namespace %s has a mismatch on collectionUUID: Collection does not exist",
								context->namespaceName)));
		}

		if (memcmp(collectionUuid->data, collection->collectionUUID.data, 16) != 0)
		{
			ereport(ERROR, (errcode(MongoCollectionUUIDMismatch),
							errmsg("Namespace %s has a mismatch on collectionUUID",
								   context->namespaceName)));
		}
	}

	List *pipelineStages = NIL;
	if (collection != NULL && collection->viewDefinition != NULL)
	{
		collection = ExtractViewDefinitionAndPipeline(databaseDatum,
													  collection->viewDefinition,
													  &pipelineStages);
	}

	context->mongoCollection = collection;

	/* First step - create the RTE.
	 * This is either a RTE for a table, or the empty table RTE.
	 * (Note that sample can modify this later in the sample stage).
	 */
	RangeTblEntry *rte = makeNode(RangeTblEntry);

	/* Match spec for ApiSchema.collection() function */
	List *colNames = list_make4(makeString("shard_key_value"), makeString("object_id"),
								makeString("document"), makeString("creation_time"));

	const char *collectionAlias = "collection";
	if (context->numNestedLevels > 0 || context->nestedPipelineLevel > 0)
	{
		StringInfo s = makeStringInfo();
		appendStringInfo(s, "collection_%d_%d", context->numNestedLevels,
						 context->nestedPipelineLevel);
		collectionAlias = s->data;
	}

	if (collection == NULL)
	{
		/* Here: Special case, if the database is config, try to see if we can create a base
		 * table out of the system metadata.
		 */
		StringView databaseView = CreateStringViewFromText(DatumGetTextPP(databaseDatum));
		if (StringViewEqualsCString(&databaseView, "config"))
		{
			Query *returnedQuery = GenerateConfigDatabaseQuery(context);
			if (returnedQuery != NULL)
			{
				return returnedQuery;
			}
		}

		rte->rtekind = RTE_FUNCTION;
		rte->relid = InvalidOid;

		rte->alias = makeAlias(collectionAlias, NIL);
		rte->eref = makeAlias(collectionAlias, colNames);
		rte->lateral = false;
		rte->inFromCl = true;
		rte->functions = NIL;
		rte->inh = false;
#if PG_VERSION_NUM >= 160000
		rte->perminfoindex = 0;
#else
		rte->requiredPerms = ACL_SELECT;
#endif
		rte->rellockmode = AccessShareLock;

		/* Now create the rtfunc*/
		FuncExpr *rangeFunc = makeFuncExpr(BsonEmptyDataTableFunctionId(), RECORDOID, NIL,
										   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
		rangeFunc->funcretset = true;
		RangeTblFunction *rangeTableFunction = makeNode(RangeTblFunction);
		rangeTableFunction->funccolcount = 4;
		rangeTableFunction->funcparams = NULL;
		rangeTableFunction->funcexpr = (Node *) rangeFunc;

		/* Add the RTFunc to the RTE */
		rte->functions = list_make1(rangeTableFunction);
	}
	else
	{
		rte->rtekind = RTE_RELATION;
		rte->relid = collection->relationId;

		rte->alias = makeAlias(collectionAlias, NIL);
		rte->eref = makeAlias(collectionAlias, colNames);
		rte->lateral = false;
		rte->inFromCl = true;
		rte->relkind = RELKIND_RELATION;
		rte->functions = NIL;
		rte->inh = true;
#if PG_VERSION_NUM >= 160000
		RTEPermissionInfo *permInfo = addRTEPermissionInfo(&query->rteperminfos, rte);
		permInfo->requiredPerms = ACL_SELECT;
#else
		rte->requiredPerms = ACL_SELECT;
#endif
		rte->rellockmode = AccessShareLock;
	}

	query->rtable = list_make1(rte);

	/* Now register the RTE in the "FROM" clause with no filters */
	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	/* Create the projector. We only project the 'document' column in this type of query */
	Var *documentEntry = CreateDocumentVar();
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	/* Now do filters - if there's no shard key we inject a default shard key of 'collectionId' */
	if (collection != NULL && collection->shardKey == NULL)
	{
		/* construct a shard_key_value = <collection_id> filter */
		Expr *zeroShardKeyFilter = CreateNonShardedShardKeyValueFilter(
			documentEntry->varno,
			collection);

		/* add the filter to WHERE */
		query->jointree->quals = (Node *) zeroShardKeyFilter;
	}

	/* Now if there's pipeline stages, apply the stages in reverse (innermost first) */
	for (int i = list_length(pipelineStages) - 1; i >= 0; i--)
	{
		bson_value_t *pipelineValue = list_nth(pipelineStages, i);
		query = MutateQueryWithPipeline(query, pipelineValue, context);
	}

	return query;
}


/*
 * Given a query, adds the cursor based functions to the query if it's a streaming query
 * and updates the queryData with the appropriate information.
 */
static void
AddCursorFunctionsToQuery(Query *query, Query *baseQuery,
						  QueryData *queryData,
						  AggregationPipelineBuildContext *context,
						  bool addCursorAsConst)
{
	if (!queryData->isStreamableCursor || queryData->isSingleBatch)
	{
		return;
	}

	/* Add the projector and percolate it down */
	if (baseQuery != query)
	{
		/*
		 * Technically we can support this for narrow scenarios like:
		 * {"$match"}{"$limit": 1 } {"$addFields"}
		 * We can just mark this as non-streamable.
		 * Note that this typically wont add any actual persistent state
		 * since it'll likely drain in 1 shot due to the $limit: 1
		 */
		elog(LOG_SERVER_ONLY, "Query has more than 1 level with streaming mode.");
		queryData->isStreamableCursor = false;
		return;
	}

	/* Add a parameter for the cursor state */
	Node *cursorNode;
	if (addCursorAsConst)
	{
		cursorNode = (Node *) MakeBsonConst(PgbsonInitEmpty());
	}
	else
	{
		context->currentParamCount++;
		queryData->cursorStateParamNumber = context->currentParamCount;
		Param *param = makeNode(Param);
		param->paramid = queryData->cursorStateParamNumber;
		param->paramkind = PARAM_EXTERN;
		param->paramtype = BsonTypeId();
		param->paramtypmod = -1;
		cursorNode = (Node *) param;
	}

	/* Create the WHERE cursor_state(document, continuationParam) and add it to the WHERE */
	List *cursorArgs = list_make2(CreateDocumentVar(), cursorNode);
	FuncExpr *cursorQual = makeFuncExpr(ApiCursorStateFunctionId(), BOOLOID,
										cursorArgs, InvalidOid, InvalidOid,
										COERCE_EXPLICIT_CALL);
	if (baseQuery->jointree->quals != NULL)
	{
		List *ands = make_ands_implicit((Expr *) baseQuery->jointree->quals);
		ands = lappend(ands, cursorQual);
		baseQuery->jointree->quals = (Node *) make_ands_explicit(ands);
	}
	else
	{
		baseQuery->jointree->quals = (Node *) cursorQual;
	}

	/* Add the cursor projector */
	TargetEntry *entry = llast(query->targetList);

	List *projectorArgs = list_make1(CreateDocumentVar());
	FuncExpr *projectionExpr = makeFuncExpr(ApiCurrentCursorStateFunctionId(),
											BsonTypeId(), projectorArgs, InvalidOid,
											InvalidOid, COERCE_EXPLICIT_CALL);

	bool resjunk = false;
	TargetEntry *newEntry = makeTargetEntry((Expr *) projectionExpr, entry->resno + 1,
											"continuation", resjunk);

	query->targetList = lappend(query->targetList, newEntry);
}


/*
 * Processes the Sample stage for the aggregation pipeline.
 * If the sample is against the base RTE - injects the Sample TSM.
 * If it's on a downstream stage, injects an ORDER BY Random().
 */
static Query *
HandleSample(const bson_value_t *existingValue, Query *query,
			 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_SAMPLE);
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation28745),
						errmsg("The $sample stage specification must be an object.")));
	}

	bson_iter_t sampleIter;
	BsonValueInitIterator(existingValue, &sampleIter);
	bson_value_t sizeValue = { 0 };

	while (bson_iter_next(&sampleIter))
	{
		if (strcmp(bson_iter_key(&sampleIter), "size") == 0)
		{
			sizeValue = *bson_iter_value(&sampleIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation28748),
							errmsg("unrecognized option to $sample")));
		}
	}

	if (sizeValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoLocation28749),
						errmsg("$sample stage must specify a size")));
	}

	if (!BsonValueIsNumber(&sizeValue))
	{
		ereport(ERROR, (errcode(MongoLocation28746),
						errmsg("size argument to $sample must be a number")));
	}

	double sizeDouble = BsonValueAsDouble(&sizeValue);

	if (sizeDouble < 0)
	{
		ereport(ERROR, (errcode(MongoLocation28747),
						errmsg("size argument to $sample must be a number")));
	}

	/* If the sample is against the base RTE - convert to a sample CTE */
	RangeTblEntry *rte = linitial(query->rtable);

	/* If there is a filter that's not the default filter then we can't push down sample */
	/* TOOD: Pushdown sample to base RTE for $lookup. */
	if (rte->rtekind == RTE_RELATION &&
		IsDefaultJoinTree(query->jointree->quals))
	{
		/* Then just convert this to a Sample RTE */
		if (rte->tablesample != NULL)
		{
			Node *sampleArg = linitial(rte->tablesample->args);
			if (!IsA(sampleArg, Const))
			{
				ereport(PANIC, (errmsg("Expected tample sample to be a const")));
			}

			Const *constVal = (Const *) sampleArg;
			int64_t finalSize = DatumGetInt64(constVal->constvalue);
			finalSize = Min(finalSize, sizeDouble);
			constVal->constvalue = Int64GetDatum(finalSize);
		}
		else
		{
			TableSampleClause *tablesample_sys_rows = makeNode(TableSampleClause);
			tablesample_sys_rows->tsmhandler = ExtensionTableSampleSystemRowsFunctionId();

			Node *rowCountArg = (Node *) makeConst(INT8OID, -1, InvalidOid,
												   sizeof(int64_t),
												   Int64GetDatum(sizeDouble), false,
												   true);

			tablesample_sys_rows->args = list_make1(rowCountArg);

			rte->tablesample = tablesample_sys_rows;
		}
	}

	/* Add an order by Random(), Limit N */
	ParseState *parseState = make_parsestate(NULL);
	parseState->p_expr_kind = EXPR_KIND_ORDER_BY;

	/* set after what is already taken */
	parseState->p_next_resno = list_length(query->targetList) + 1;

	Expr *expr = (Expr *) makeFuncExpr(PgRandomFunctionOid(), FLOAT8OID, NIL,
									   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	SortBy *sortBy = makeNode(SortBy);
	sortBy->location = -1;
	sortBy->sortby_dir = SORTBY_DEFAULT; /* reset later */
	sortBy->node = (Node *) expr;

	bool resjunk = true;
	TargetEntry *sortEntry = makeTargetEntry((Expr *) expr,
											 (AttrNumber) parseState->p_next_resno++,
											 "?sort?",
											 resjunk);
	query->targetList = lappend(query->targetList, sortEntry);
	List *sortlist = addTargetToSortList(parseState, sortEntry,
										 NIL, query->targetList, sortBy);

	pfree(parseState);
	if (sortlist == NIL)
	{
		ereport(ERROR, (errcode(MongoLocation15976),
						errmsg("$sort stage must have at least one sort key")));
	}

	query->sortClause = sortlist;

	query->limitCount = (Node *) makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
										   Int64GetDatum(sizeDouble), false, true);

	/* Push next stage to a new subquery (since we did a sort) */
	context->requiresSubQuery = true;

	return query;
}


/*
 * Helper method used by MutateStageWithPipeline to extract the appropriate
 * Stage information. Compares the aggregation stage by the ordinal comparison
 * of its name.
 */
static int
CompareStageByStageName(const void *a, const void *b)
{
	const char *key = (const char *) a;
	AggregationStageDefinition *stage = (AggregationStageDefinition *) b;

	return strcmp(key, stage->stage);
}


/*
 * Whether or not the quals is the default
 * shard_key_value = 'int'
 */
static bool
IsDefaultJoinTree(Node *node)
{
	if (node == NULL)
	{
		return true;
	}

	if (!IsA(node, OpExpr))
	{
		return false;
	}

	OpExpr *opExpr = (OpExpr *) node;
	return opExpr->opno == BigintEqualOperatorId();
}


/*
 * Default helper for a stage that can always be inlined for a $lookup such as $match
 */
static bool
CanInlineLookupStageTrue(const bson_value_t *stageValue, const StringView *lookupPath)
{
	return true;
}


/*
 * Helper for a projection stage on whether it can be inlined for a $lookup.
 * Can be inlined if all the projection functions are not prefixes or suffixes
 * of the lookup local path.
 * We can probably do better but that is left as an exercise for later.
 */
static bool
CanInlineLookupStageProjection(const bson_value_t *stageValue, const
							   StringView *lookupPath)
{
	if (stageValue->value_type != BSON_TYPE_DOCUMENT)
	{
		return false;
	}

	bson_iter_t projectIter;
	BsonValueInitIterator(stageValue, &projectIter);

	while (bson_iter_next(&projectIter))
	{
		const StringView keyView = bson_iter_key_string_view(&projectIter);
		if (StringViewStartsWithStringView(&keyView, lookupPath) ||
			StringViewStartsWithStringView(lookupPath, &keyView))
		{
			return false;
		}
	}

	return true;
}


/*
 * Helper for an unset stage on whether it can be inlined for a $lookup.
 */
static bool
CanInlineLookupStageUnset(const bson_value_t *stageValue, const StringView *lookupPath)
{
	/* An unset can be pushed up if */
	if (stageValue->value_type == BSON_TYPE_UTF8)
	{
		/* a) it is a string and it's not equal to or a suffix of the lookup path */
		StringView unsetStr = {
			.string = stageValue->value.v_utf8.str, .length = stageValue->value.v_utf8.len
		};
		return !StringViewStartsWithStringView(&unsetStr, lookupPath);
	}
	else if (stageValue->value_type == BSON_TYPE_ARRAY)
	{
		/* or b) it's an array, and none of the unset paths intersect with the lookup path */
		bson_iter_t unsetArrayIter;
		BsonValueInitIterator(stageValue, &unsetArrayIter);

		bool allIndependentOfLookup = true;
		while (bson_iter_next(&unsetArrayIter) && allIndependentOfLookup)
		{
			if (BSON_ITER_HOLDS_UTF8(&unsetArrayIter))
			{
				StringView unsetStr = { 0 };
				unsetStr.string = bson_iter_utf8(&unsetArrayIter, &unsetStr.length);
				allIndependentOfLookup = !StringViewStartsWithStringView(&unsetStr,
																		 lookupPath);
			}
			else
			{
				allIndependentOfLookup = false;
			}
		}

		return allIndependentOfLookup;
	}

	return false;
}


/*
 * Helper for an unwind stage on whether it can be inlined for a $lookup.
 */
static bool
CanInlineLookupStageUnwind(const bson_value_t *stageValue, const StringView *lookupPath)
{
	/* An unwind can be pushed up if */
	if (stageValue->value_type == BSON_TYPE_UTF8)
	{
		/* a) it is a string and it's not equal to or a suffix of the lookup path */
		StringView unwindStr = {
			.string = stageValue->value.v_utf8.str, .length = stageValue->value.v_utf8.len
		};
		return !StringViewStartsWithStringView(&unwindStr, lookupPath);
	}
	else if (stageValue->value_type == BSON_TYPE_DOCUMENT)
	{
		/* or b) it's an object, and the unwind path here is a suffix of the lookup path */
		bson_iter_t optionsDocIter;
		BsonValueInitIterator(stageValue, &optionsDocIter);
		if (bson_iter_find(&optionsDocIter, "path") &&
			BSON_ITER_HOLDS_UTF8(&optionsDocIter))
		{
			StringView unwindStr = { 0 };
			unwindStr.string = bson_iter_utf8(&optionsDocIter, &unwindStr.length);
			return !StringViewStartsWithStringView(&unwindStr, lookupPath);
		}
	}

	return false;
}


/*
 * parses the 'cursor' field in a query or aggregation spec.
 */
void
ParseCursorDocument(bson_iter_t *iterator, QueryData *queryData)
{
	EnsureTopLevelFieldType("cursor", iterator, BSON_TYPE_DOCUMENT);
	bson_iter_t cursorDocIter;
	if (!bson_iter_recurse(iterator, &cursorDocIter))
	{
		return;
	}

	while (bson_iter_next(&cursorDocIter))
	{
		const char *path = bson_iter_key(&cursorDocIter);
		const bson_value_t *value = bson_iter_value(&cursorDocIter);
		if (strcmp(path, "batchSize") == 0)
		{
			SetBatchSize("cursor.batchSize", value, queryData);
		}
		else if (strcmp(path, "singleBatch") == 0)
		{
			EnsureTopLevelFieldType("cursor.singleBatch", &cursorDocIter, BSON_TYPE_BOOL);
			if (value->value.v_bool)
			{
				queryData->isSingleBatch = true;
			}
		}
		else
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("Unrecognized field: %s",
								   path)));
		}
	}
}


/*
 * Simplify an Aggregation request. Checks that the RTE
 * of the query is a function RTE for the query operation
 * If the RTE is for the specified function, then replaces
 * the original RTE with the specified query as a subquery.
 *
 * Note that this creates a new RTE and does not modify the existing one
 * since the existing one Must still be a Function RTE.
 */
static void
TryHandleSimplifyAggregationRequest(SupportRequestSimplify *simplifyRequest)
{
	if (list_length(simplifyRequest->root->parse->rtable) != 1)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"Query pipeline function must be the only entry in the FROM clause")));
	}

	RangeTblEntry *rte = (RangeTblEntry *) linitial(simplifyRequest->root->parse->rtable);
	if (rte->rtekind != RTE_FUNCTION || list_length(rte->functions) != 1)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"Query pipeline FROM clause must have exactly 1 function")));
	}

	RangeTblFunction *rangeTableFunc = linitial(rte->functions);
	FuncExpr *funcExpr = (FuncExpr *) rangeTableFunc->funcexpr;
	if (!equal(funcExpr, simplifyRequest->fcall))
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Query pipeline function must be in the FROM clause")));
	}

	/* Now that we know that the query pipeline function is the only Function in the RTE */
	/* We replace the RTE with the query from it */
	Node *databaseArg = linitial(funcExpr->args);
	Node *secondArg = lsecond(funcExpr->args);
	if (!IsA(secondArg, Const) || !IsA(databaseArg, Const))
	{
		/* Let the runtime deal with this (This will either go to the runtime function and fail, or noop due to prepared and come back here
		 * to be evaluated during the EXECUTE)
		 */
		return;
	}

	Const *databaseConst = (Const *) databaseArg;
	Const *aggregationConst = (Const *) secondArg;
	if (databaseConst->constisnull || aggregationConst->constisnull)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"Query pipeline arguments should not be null.")));
	}

	if (simplifyRequest->fcall->funcid == ApiCatalogAggregationPipelineFunctionId())
	{
		pgbson *pipeline = DatumGetPgBson(aggregationConst->constvalue);
		QueryData queryData = { 0 };
		bool enableCursorParam = false;
		Query *aggregationQuery = GenerateAggregationQuery(databaseConst->constvalue,
														   pipeline, &queryData,
														   enableCursorParam);

		/* Now that we have the query, swap out the RTE with this */
		RangeTblEntry *newEntry = MakeSubQueryRte(aggregationQuery, 0, 0, "topQuery",
												  true);
		simplifyRequest->root->parse->rtable = list_make1(newEntry);
	}
	else if (simplifyRequest->fcall->funcid == ApiCatalogAggregationFindFunctionId())
	{
		pgbson *pipeline = DatumGetPgBson(aggregationConst->constvalue);
		QueryData queryData = { 0 };
		bool enableCursorParam = false;
		Query *findQuery = GenerateFindQuery(databaseConst->constvalue, pipeline,
											 &queryData,
											 enableCursorParam);

		/* Now that we have the query, swap out the RTE with this */
		RangeTblEntry *newEntry = MakeSubQueryRte(findQuery, 0, 0, "topQuery", true);
		simplifyRequest->root->parse->rtable = list_make1(newEntry);
	}
	else if (simplifyRequest->fcall->funcid == ApiCatalogAggregationCountFunctionId())
	{
		pgbson *pipeline = DatumGetPgBson(aggregationConst->constvalue);
		Query *countQuery = GenerateCountQuery(databaseConst->constvalue, pipeline);

		/* Now that we have the query, swap out the RTE with this */
		RangeTblEntry *newEntry = MakeSubQueryRte(countQuery, 0, 0, "topQuery", true);
		simplifyRequest->root->parse->rtable = list_make1(newEntry);
	}
	else if (simplifyRequest->fcall->funcid == ApiCatalogAggregationDistinctFunctionId())
	{
		pgbson *pipeline = DatumGetPgBson(aggregationConst->constvalue);
		Query *distinctQuery = GenerateDistinctQuery(databaseConst->constvalue, pipeline);

		/* Now that we have the query, swap out the RTE with this */
		RangeTblEntry *newEntry = MakeSubQueryRte(distinctQuery, 0, 0, "topQuery", true);
		simplifyRequest->root->parse->rtable = list_make1(newEntry);
	}
	else if (simplifyRequest->fcall->funcid == ApiCollectionFunctionId())
	{
		AggregationPipelineBuildContext baseContext = { 0 };
		baseContext.databaseNameDatum = databaseConst->constvalue;
		text *collectionText = DatumGetTextP(aggregationConst->constvalue);
		StringView collectionView = CreateStringViewFromText(collectionText);

		pg_uuid_t *collectionUuid = NULL;
		Query *collectionQuery = GenerateBaseTableQuery(databaseConst->constvalue,
														&collectionView,
														collectionUuid,
														&baseContext);

		/* Now that we have the query, swap out the RTE with this */
		RangeTblEntry *newEntry = MakeSubQueryRte(collectionQuery, 0, 0, "collection",
												  true);
		simplifyRequest->root->parse->rtable = list_make1(newEntry);
	}
}


/* Check $match stage for sort clause. If there is one, $near or $nearSphere was used which is not allowed. */
static void
ValidateQueryTreeForMatchStage(const Query *query)
{
	if (!query->sortClause)
	{
		return;
	}


	bool isGeoNear = false;
	ListCell *cell;
	foreach(cell, query->sortClause)
	{
		SortGroupClause *sortClause = (SortGroupClause *) lfirst(cell);
		TargetEntry *tle = get_sortgroupclause_tle(sortClause, query->targetList);
		if (tle->resjunk && IsA(tle->expr, OpExpr) &&
			((OpExpr *) tle->expr)->opno == BsonGeonearDistanceOperatorId())
		{
			if (isGeoNear)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("Too many geoNear expressions")));
			}
			isGeoNear = true;
		}
	}

	if (!isGeoNear)
	{
		return;
	}

	TargetEntry *targetEntry = linitial(query->targetList);
	Node *node = (Node *) targetEntry->expr;

	if (!(IsA(node, FuncExpr) && CheckFuncExprBsonDollarProjectGeonear(
			  (FuncExpr *) node)))
	{
		ThrowGeoNearNotAllowedInContextError();
	}
}


/*
 * Recursively check all func project func expression to check if it has $geoNear project
 * bson_dollar_add(bson_dollar_project...)
 */
static bool
CheckFuncExprBsonDollarProjectGeonear(const FuncExpr *funcExpr)
{
	CHECK_FOR_INTERRUPTS();
	bool isGeoNearProject = false;

	if (funcExpr->funcid == BsonDollarProjectGeonearFunctionOid())
	{
		isGeoNearProject = true;
	}
	else
	{
		/* loop all args */
		ListCell *cell;
		foreach(cell, funcExpr->args)
		{
			Expr *expr = (Expr *) lfirst(cell);
			if (IsA(expr, FuncExpr))
			{
				isGeoNearProject =
					CheckFuncExprBsonDollarProjectGeonear((FuncExpr *) expr);

				if (isGeoNearProject)
				{
					break;
				}
			}
		}
	}

	return isGeoNearProject;
}


/*
 * Handles $match aggregation stage and validates the query tree for geospatial queries.
 */
static Query *
HandleMatchAggregationStage(const bson_value_t *existingValue, Query *query,
							AggregationPipelineBuildContext *context)
{
	query = HandleMatch(existingValue, query, context);
	ValidateQueryTreeForMatchStage(query);
	return query;
}
