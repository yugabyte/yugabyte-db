/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_pipeline.c
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
#include <utils/lsyscache.h>
#include <utils/fmgroids.h>
#include <nodes/supportnodes.h>
#include <parser/parse_relation.h>
#include <parser/parse_func.h>

#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "planner/documentdb_planner.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_aggregation_window_operators.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "commands/defrem.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "aggregation/bson_query.h"

#include "aggregation/bson_aggregation_pipeline_private.h"
#include "api_hooks.h"
#include "vector/vector_common.h"
#include "aggregation/bson_project.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "operators/bson_expression_bucket_operator.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "aggregation/bson_densify.h"
#include "collation/collation.h"
#include "api_hooks.h"

extern bool EnableCursorsOnAggregationQueryRewrite;
extern bool EnableLookupUnwindSupport;
extern bool EnableCollation;
extern bool DefaultInlineWriteOperations;
extern bool EnableSimplifyGroupAccumulators;
extern bool EnableSortbyIdPushDownToPrimaryKey;
extern int MaxAggregationStagesAllowed;

/* GUC to config tdigest compression */
extern int TdigestCompressionAccuracy;

/*
 * The mutation function that modifies a given query with a pipeline stage's value.
 */
typedef Query *(*MutateQueryForStageFunc)(const bson_value_t *existingValue,
										  Query *query,
										  AggregationPipelineBuildContext *context);

/*
 * This function checks the compatibility of stages in the pipeline.
 */
typedef void (*PipelineStagesPreCheckFunc)(const bson_value_t *existingValue,
										   const AggregationPipelineBuildContext *context);

/*
 * Whether or not the stage requires persistence on the cursor.
 */
typedef bool (*RequiresPersistentCursorFunc)(const bson_value_t *existingValue);

/*
 * Whether or not the stage can be inlined for a lookup stage
 */
typedef bool (*CanInlineLookupStage)(const bson_value_t *stageValue, const
									 StringView *lookupPath, bool hasLet);


/*
 * Declaration of a given aggregation pipeline stage.
 */
typedef struct AggregationStageDefinition
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

	/* Check whether given pipeline stages are compatible. */
	PipelineStagesPreCheckFunc pipelineCheckFunc;

	/* Whether or not the stage is an output stage. $merge and $out are output stages */
	bool isOutputStage;

	/* Allow Base shard table pushdown */
	bool allowBaseShardTablePushdown;

	Stage stageEnum;
} AggregationStageDefinition;


static void AddCursorFunctionsToQuery(Query *query, Query *baseQuery,
									  QueryData *queryData,
									  AggregationPipelineBuildContext *context,
									  bool addCursorAsConst);
static void AddQualifierForTailableQuery(Query *query, Query *baseQuery,
										 QueryData *queryData,
										 AggregationPipelineBuildContext *
										 context);
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
static Query * HandleFill(const bson_value_t *existingValue, Query *query,
						  AggregationPipelineBuildContext *context);
static Query * HandleLimit(const bson_value_t *existingValue, Query *query,
						   AggregationPipelineBuildContext *context);
static Query * HandleProject(const bson_value_t *existingValue, Query *query,
							 AggregationPipelineBuildContext *context);
static Query * HandleProjectFind(const bson_value_t *existingValue,
								 const bson_value_t *queryValue, Query *query,
								 AggregationPipelineBuildContext *context);
static Query * HandleRedact(const bson_value_t *existingValue, Query *query,
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

static bool HasShardKeyFilterWalker(Node *node, void *state);
static bool CanSortByObjectId(Query *query, AggregationPipelineBuildContext *context);
static bool CanInlineLookupStageProjection(const bson_value_t *stageValue, const
										   StringView *lookupPath, bool hasLet);
static bool CanInlineLookupStageUnset(const bson_value_t *stageValue, const
									  StringView *lookupPath, bool hasLet);
static bool CanInlineLookupStageUnwind(const bson_value_t *stageValue, const
									   StringView *lookupPath, bool hasLet);
static bool CanInlineLookupStageTrue(const bson_value_t *stageValue, const
									 StringView *lookupPath, bool hasLet);
static void PreCheckChangeStreamPipelineStages(const bson_value_t *pipelineValue,
											   const AggregationPipelineBuildContext *
											   context);
static bool CanInlineLookupStageMatch(const bson_value_t *stageValue, const
									  StringView *lookupPath, bool hasLet);

static bool CheckFuncExprBsonDollarProjectGeonear(const FuncExpr *funcExpr);

static void ValidateQueryTreeForMatchStage(const Query *query);
static void RewriteFillToSetWindowFieldsSpec(const bson_value_t *fillSpec,
											 bool *hasSortBy,
											 bool *onlyHasValueFill,
											 bson_value_t *sortSpec,
											 bson_value_t *addFieldsForValueFill,
											 bson_value_t *setWindowFieldsSpec,
											 bson_value_t *partitionByFields);
static void TryOptimizeAggregationPipelines(List **aggregationStages,
											AggregationPipelineBuildContext *context);

#define COMPATIBLE_CHANGE_STREAM_STAGES_COUNT 8
const char *CompatibleChangeStreamPipelineStages[COMPATIBLE_CHANGE_STREAM_STAGES_COUNT] =
{
	"$match",
	"$project",
	"$addFields",
	"$replaceRoot",
	"$replaceWith",
	"$set",
	"$unset",
	"$redact"
};

static const AggregationStageDefinition LookupUnwindStageDefinition = {
	.stage = "$lookupUnwind",
	.mutateFunc = &HandleLookupUnwind,
	.requiresPersistentCursor = &RequiresPersistentCursorTrue,
	.canInlineLookupStageFunc = &CanInlineLookupStageLookup,

	/* Lookup preserves order of the Left collection */
	.preservesStableSortOrder = true,
	.canHandleAgnosticQueries = false,
	.isProjectTransform = false,
	.isOutputStage = false,
	.pipelineCheckFunc = NULL,
	.allowBaseShardTablePushdown = false,
	.stageEnum = Stage_LookupUnwind,
};

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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Internal_InhibitOptimization,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_AddFields,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Bucket,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_BucketAuto,
	},
	{
		.stage = "$changeStream",
		.mutateFunc = &HandleChangeStream,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = true,
		.isProjectTransform = false,
		.isOutputStage = false,
		.pipelineCheckFunc = PreCheckChangeStreamPipelineStages,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_ChangeStream,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_CollStats,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Count,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_CurrentOp,
	},
	{
		.stage = "$densify",
		.mutateFunc = &HandleDensify,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Densify,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_Documents,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_Facet,
	},
	{
		.stage = "$fill",
		.mutateFunc = &HandleFill,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Fill,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_GeoNear,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_GraphLookup,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Group,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_IndexStats,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_InverseMatch,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Limit,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_ListLocalSessions,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_ListSessions,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_Lookup,
	},
	{
		.stage = "$match",
		.mutateFunc = &HandleMatchAggregationStage,
		.requiresPersistentCursor = &RequiresPersistentCursorFalse,

		/* can always be inlined since it doesn't change the projector */
		.canInlineLookupStageFunc = &CanInlineLookupStageMatch,

		/* Match does not change the output format */
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Match,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_Merge,
	},
	{
		.stage = "$out",
		.mutateFunc = &HandleOut,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = true,
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_Out,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Project,
	},
	{
		.stage = "$redact",
		.mutateFunc = &HandleRedact,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = true,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = true,
		.isOutputStage = false,
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Redact,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_ReplaceRoot,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_ReplaceWith,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Sample,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Search,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_SearchMeta,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Set,
	},
	{
		.stage = "$setWindowFields",
		.mutateFunc = &HandleSetWindowFields,
		.requiresPersistentCursor = &RequiresPersistentCursorTrue,
		.canInlineLookupStageFunc = NULL,
		.preservesStableSortOrder = false,
		.canHandleAgnosticQueries = false,
		.isProjectTransform = false,
		.isOutputStage = false,
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_SetWindowFields,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Skip,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Sort,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_SortByCount,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_UnionWith,
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
		.pipelineCheckFunc = NULL,
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Unset,
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
		.allowBaseShardTablePushdown = true,
		.stageEnum = Stage_Unwind,
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
		.pipelineCheckFunc = NULL,

		/* $vectorSearch is needed to be executed withing custom scan boundaries see EvaluateMetaSearchScore in vector/vector_utilities.c */
		.allowBaseShardTablePushdown = false,
		.stageEnum = Stage_VectorSearch,
	}
};

static const int AggregationStageCount = sizeof(StageDefinitions) /
										 sizeof(AggregationStageDefinition);

static const int MaxEvenFunctionArguments = ((int) (FUNC_MAX_ARGS / 2)) * 2;

PG_FUNCTION_INFO_V1(command_bson_aggregation_pipeline);
PG_FUNCTION_INFO_V1(command_api_collection);
PG_FUNCTION_INFO_V1(command_aggregation_support);
PG_FUNCTION_INFO_V1(documentdb_core_bson_to_bson);


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
 * Creates a Var in the query representing the 'document' column of a documentdb_data table.
 */
inline static Var *
CreateDocumentVar(void)
{
	/* the only Var in the Query context (if any) */
	Index varno = 1;

	/* not lives in a subquery */
	Index varlevelsup = 0;
	return makeVar(varno, DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER,
				   BsonTypeId(), DOCUMENT_DATA_TABLE_DOCUMENT_VAR_TYPMOD,
				   DOCUMENT_DATA_TABLE_DOCUMENT_VAR_COLLATION, varlevelsup);
}


/*
 * Creates a Var in the query representing the 'document' column of a documentdb_data table.
 */
inline static Var *
CreateChangeStreamDocumentVar(void)
{
	/* the only Var in the Query context (if any) */
	Index varno = 1;

	/* not lives in a subquery */
	Index varlevelsup = 0;
	return makeVar(varno, DOCUMENT_CHANGE_STREAM_TABLE_DOCUMENT_VAR_ATTR_NUMBER,
				   BsonTypeId(), DOCUMENT_DATA_TABLE_DOCUMENT_VAR_TYPMOD,
				   DOCUMENT_DATA_TABLE_DOCUMENT_VAR_COLLATION, varlevelsup);
}


/*
 * Creates a Var in the query representing the 'document' column of a documentdb_data table.
 */
inline static Var *
CreateChangeStreamContinuationtVar(void)
{
	/* the only Var in the Query context (if any) */
	Index varno = 1;

	/* not lives in a subquery */
	Index varlevelsup = 0;
	return makeVar(varno, DOCUMENT_CHANGE_STREAM_TABLE_CONTINUATION_VAR_ATTR_NUMBER,
				   BsonTypeId(), DOCUMENT_DATA_TABLE_DOCUMENT_VAR_TYPMOD,
				   DOCUMENT_DATA_TABLE_DOCUMENT_VAR_COLLATION, varlevelsup);
}


/*
 * Helper function to limit the number of aggregation stages in a pipeline.
 */
inline static void
CheckMaxAllowedAggregationStages(int numberOfStages)
{
	if (numberOfStages > MaxAggregationStagesAllowed)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION7749501),
						errmsg("Pipeline length must be no longer than %d stages.",
							   MaxAggregationStagesAllowed)));
	}
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
 * This function currently is pseudo-deprecated
 */
Datum
command_aggregation_support(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(NIL);
}


/*
 * Converts a documentdb_core.bson to a bson.
 */
Datum
documentdb_core_bson_to_bson(PG_FUNCTION_ARGS)
{
	return PG_GETARG_DATUM(0);
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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
	List *stages = ExtractAggregationStages(pipelineValue,
											&validationContext);
	validationQuery = MutateQueryWithPipeline(validationQuery, stages,
											  &validationContext);
	pfree(validationQuery);
}


/*
 * Core logic that takes a pipeline array specified by pipelineValue,
 * and updates the "query" with the pipeline stages' effects.
 * The mutated query is returned as an output.
 */
Query *
MutateQueryWithPipeline(Query *query, List *aggregationStages,
						AggregationPipelineBuildContext *context)
{
	/* Apply stage transformations now */
	ListCell *stageCell = NULL;
	foreach(stageCell, aggregationStages)
	{
		AggregationStage *stage = (AggregationStage *) lfirst(stageCell);
		const AggregationStageDefinition *definition = stage->stageDefinition;
		const char *stageName = definition->stage;

		if (query->jointree->fromlist == NIL && !definition->canHandleAgnosticQueries)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
							errmsg(
								"{aggregate: 1} is not valid for '%s'; a collection is required.",
								stageName),
							errdetail_log(
								"{aggregate: 1} is not valid for '%s'; a collection is required.",
								stageName)));
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
				if (context->requiresTailableCursor)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
									errmsg(
										"Cannot use tailable cursor with stage %s",
										stageName)));
				}

				/* Not a project, so push to a subquery */
				query = MigrateQueryToSubQuery(query, context);
			}
		}

		query = definition->mutateFunc(&stage->stageValue, query,
									   context);

		context->requiresPersistentCursor =
			context->requiresPersistentCursor ||
			definition->requiresPersistentCursor(&stage->stageValue);

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
						 bool addCursorParams, bool setStatementTimeout)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = database;
	context.optimizePipelineStages = true;
	queryData->cursorKind = QueryCursorType_Unspecified;

	bson_iter_t aggregationIterator;
	PgbsonInitIterator(aggregationSpec, &aggregationIterator);

	StringView collectionName = { 0 };
	bson_value_t pipelineValue = { 0 };
	bson_value_t let = { 0 };
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
		else if (StringViewEqualsCString(&keyView, "let"))
		{
			bool hasValue = EnsureTopLevelFieldTypeNullOkUndefinedOK("let",
																	 &aggregationIterator,
																	 BSON_TYPE_DOCUMENT);
			if (hasValue)
			{
				let = *value;
			}
		}
		else if (StringViewEqualsCString(&keyView, "collectionUUID"))
		{
			EnsureTopLevelFieldType("collectionUUID", &aggregationIterator,
									BSON_TYPE_BINARY);
			if (value->value.v_binary.subtype != BSON_SUBTYPE_UUID ||
				value->value.v_binary.data_len != 16)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
									"field collectionUUID must be of UUID type")));
			}

			collectionUuid = palloc(sizeof(pg_uuid_t));
			memcpy(&collectionUuid->data, value->value.v_binary.data, 16);
		}
		else if (EnableCollation && StringViewEqualsCString(&keyView, "collation"))
		{
			EnsureTopLevelFieldType("collation", &aggregationIterator,
									BSON_TYPE_DOCUMENT);
			ReportFeatureUsage(FEATURE_COLLATION);
			ParseAndGetCollationString(value, context.collationString);
		}
		else if (setStatementTimeout &&
				 StringViewEqualsCString(&keyView, "maxTimeMS"))
		{
			EnsureTopLevelFieldIsNumberLike("find.maxTimeMS", value);
			SetExplicitStatementTimeout(BsonValueAsInt32(value));
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("%*s is an unknown field",
								   keyView.length, keyView.string)));
		}
	}

	if (pipelineValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
							"Required variable pipeline must be valid")));
	}

	if (collectionName.length == 0 && !isCollectionAgnosticQuery)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
							"Required variables aggregate must be valid")));
	}

	pgbson *parsedVariables = ParseAndGetTopLevelVariableSpec(&let,
															  &queryData->
															  timeSystemVariables);

	if (parsedVariables != NULL && !IsPgbsonEmptyDocument(parsedVariables))
	{
		context.variableSpec = (Expr *) MakeBsonConst(parsedVariables);
	}

	List *aggregationStages = ExtractAggregationStages(&pipelineValue,
													   &context);

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

	query = MutateQueryWithPipeline(query, aggregationStages, &context);

	if (context.requiresTailableCursor)
	{
		queryData->cursorKind = QueryCursorType_Tailable;

		/*
		 * change stream is the only stage that requires a tailable cursor.
		 * Since change stream manages the continuation handling by itself,
		 * there is no need to add cursor params to the query.
		 */
		addCursorParams = false;
	}
	else if (query->commandType == CMD_MERGE)
	{
		/* CMD_MERGE is case when pipeline has output stage ($merge or $out) result will be always single batch. */
		queryData->cursorKind = QueryCursorType_SingleBatch;
	}
	else if (queryData->cursorKind == QueryCursorType_Unspecified)
	{
		queryData->cursorKind =
			context.requiresPersistentCursor || isCollectionAgnosticQuery ?
			QueryCursorType_Persistent : QueryCursorType_Streamable;
	}

	queryData->namespaceName = context.namespaceName;

	/* This is validated *after* the pipeline parsing happens */
	if (!hasCursor && !explain && addCursorParams)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
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
	else if (queryData->cursorKind == QueryCursorType_Tailable)
	{
		AddQualifierForTailableQuery(query, baseQuery, queryData,
									 &context);
	}

	return query;
}


/*
 * Applies a find spec against a query and expands it into the underlying SQL AST.
 */
Query *
GenerateFindQuery(Datum databaseDatum, pgbson *findSpec, QueryData *queryData, bool
				  addCursorParams, bool setStatementTimeout)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = databaseDatum;

	/* Queries start out as persistent cursor */
	queryData->cursorKind = QueryCursorType_Unspecified;

	bson_iter_t findIterator;
	PgbsonInitIterator(findSpec, &findIterator);

	StringView collectionName = { 0 };
	bool hasFind = false;
	bool hasNtoreturn = false;
	bool hasBatchSize = false;
	bool isNtoReturnSupported = IsNtoReturnSupported();
	bson_value_t filter = { 0 };
	bson_value_t limit = { 0 };
	bson_value_t projection = { 0 };
	bson_value_t sort = { 0 };
	bson_value_t skip = { 0 };
	bson_value_t let = { 0 };
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
			/* In case ntoreturn is present and has been parsed already we throw this error */
			if (!isNtoReturnSupported && hasNtoreturn)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"'limit' or 'batchSize' fields can not be set with 'ntoreturn' field")));
			}

			/* Validation handled in the stage processing */
			limit = *value;
		}
		else if (StringViewEqualsCString(&keyView, "projection"))
		{
			/* Validation handled in the stage processing */
			/* TODO - Mongo validates projection even if collection is not present */
			/* to align with that we may need to validate projection here, like $elemMatch envolve $jsonSchema */
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
		else if (EnableCollation && StringViewEqualsCString(&keyView, "collation"))
		{
			EnsureTopLevelFieldType("collation", &findIterator, BSON_TYPE_DOCUMENT);
			ReportFeatureUsage(FEATURE_COLLATION);
			ParseAndGetCollationString(value, context.collationString);
		}
		else if (StringViewEqualsCString(&keyView, "singleBatch"))
		{
			EnsureTopLevelFieldType("singleBatch", &findIterator, BSON_TYPE_BOOL);
			if (value->value.v_bool)
			{
				queryData->cursorKind = QueryCursorType_SingleBatch;
			}
		}
		else if (StringViewEqualsCString(&keyView, "batchSize"))
		{
			/* In case ntoreturn is present and has been parsed already we throw this error */
			if (!isNtoReturnSupported && hasNtoreturn)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"'limit' or 'batchSize' fields can not be set with 'ntoreturn' field")));
			}
			SetBatchSize("batchSize", value, queryData);
			hasBatchSize = !isNtoReturnSupported;
		}
		else if (StringViewEqualsCString(&keyView, "ntoreturn"))
		{
			/* In case of versions <6.0 we support ntoreturn */
			if (isNtoReturnSupported)
			{
				SetBatchSize("ntoreturn", value, queryData);
			}

			/* In case ntoreturn is the last option in the find command we first check if batchSize or limit is present */
			if (limit.value_type != BSON_TYPE_EOD || hasBatchSize)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"'limit' or 'batchSize' fields can not be set with 'ntoreturn' field")));
			}
			hasNtoreturn = !isNtoReturnSupported;
		}
		else if (StringViewEqualsCString(&keyView, "let"))
		{
			EnsureTopLevelFieldType("let", &findIterator, BSON_TYPE_DOCUMENT);
			let = *value;
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("key %.*s is not supported yet",
								   keyView.length, keyView.string),
							errdetail_log("key %.*s is not supported yet",
										  keyView.length, keyView.string)));
		}
		else if (setStatementTimeout &&
				 StringViewEqualsCString(&keyView, "maxTimeMS"))
		{
			EnsureTopLevelFieldIsNumberLike("find.maxTimeMS", value);
			SetExplicitStatementTimeout(BsonValueAsInt32(value));
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string),
							errdetail_log("%.*s is an unknown field",
										  keyView.length, keyView.string)));
		}
	}

	if (!hasFind)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Required element \"find\" missing.")));
	}
	else if (collectionName.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
						errmsg("Collection name can't be empty.")));
	}

	/* In case only ntoreturn is present we give a different error.*/
	if (!isNtoReturnSupported && hasNtoreturn)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5746102),
						errmsg("Command is not supported for mongo version >= 5.1")));
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

	pgbson *parsedVariables = ParseAndGetTopLevelVariableSpec(&let,
															  &queryData->
															  timeSystemVariables);

	if (parsedVariables != NULL && !IsPgbsonEmptyDocument(parsedVariables))
	{
		context.variableSpec = (Expr *) MakeBsonConst(parsedVariables);
	}

	if (sort.value_type != BSON_TYPE_EOD)
	{
		context.requiresPersistentCursor = true;
	}

	if (RequiresPersistentCursorSkip(&skip))
	{
		context.requiresPersistentCursor = true;
	}

	if (RequiresPersistentCursorLimit(&limit))
	{
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

	if (rt_fetch(1, query->rtable)->rtekind != RTE_RELATION)
	{
		/* Any attempts to push to a subquery should invalidate point read plans */
		context.isPointReadQuery = false;
	}

	if (queryData->cursorKind == QueryCursorType_Unspecified)
	{
		queryData->cursorKind = context.requiresPersistentCursor ?
								QueryCursorType_Persistent : QueryCursorType_Streamable;
	}

	queryData->namespaceName = context.namespaceName;
	if (context.isPointReadQuery &&
		context.allowShardBaseTable && queryData->batchSize >= 1)
	{
		/* If we're still targeting the local shard && we have a point read
		 * Mark the query for a point read plan.
		 */
		queryData->cursorKind = QueryCursorType_PointRead;
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
 * Generates a query that is akin to the MongoDB $count query command
 */
Query *
GenerateCountQuery(Datum databaseDatum, pgbson *countSpec, bool setStatementTimeout)
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
		else if (setStatementTimeout && StringViewEqualsCString(&keyView, "maxTimeMS"))
		{
			EnsureTopLevelFieldIsNumberLike("distinct.maxTimeMS", value);
			SetExplicitStatementTimeout(BsonValueAsInt32(value));
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string)));
		}
	}

	if (collectionName.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
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
			if (skip.value_type != BSON_TYPE_NULL)
			{
				if (!BsonValueIsNumber(&skip))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH)),
							errmsg(
								"BSON field 'skip' is the wrong type '%s', expected types '[long, int, decimal, double]'",
								BsonTypeName(skip.value_type)),
							errdetail_log(
								"BSON field 'skip' is the wrong type '%s', expected types '[long, int, decimal, double]'",
								BsonTypeName(skip.value_type)));
				}

				int64_t skipValue = BsonValueAsInt64(&skip);
				if (skipValue < 0)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51024)),
							errmsg(
								"BSON field 'skip' value must be >=0, actual value '%ld'",
								skipValue),
							errdetail_log(
								"BSON field 'skip' value must be >=0, actual value '%ld'",
								skipValue));
				}

				query = HandleSkip(&skip, query, &context);
			}
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
										BsonDollaMergeDocumentsFunctionOid(), NULL, NULL);

	return query;
}


/*
 * Generates a query that is akin to the MongoDB $distinct query command
 */
Query *
GenerateDistinctQuery(Datum databaseDatum, pgbson *distinctSpec, bool setStatementTimeout)
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
		else if (setStatementTimeout && StringViewEqualsCString(&keyView, "maxTimeMS"))
		{
			EnsureTopLevelFieldIsNumberLike("distinct.maxTimeMS", value);
			SetExplicitStatementTimeout(BsonValueAsInt32(value));
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("%.*s is an unknown field",
								   keyView.length, keyView.string)));
		}
	}

	if (!hasDistinct)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Required element \"distinct\" missing.")));
	}
	else if (collectionName.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
						errmsg("Collection name can't be empty.")));
	}

	if (distinctKey.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("distinct key can't be empty.")));
	}

	if (strlen(distinctKey.string) != distinctKey.length)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_KEYCANNOTCONTAINNULLBYTE),
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
ParseGetMore(text *databaseName, pgbson *getMoreSpec, QueryData *queryData, bool
			 setStatementTimeout)
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
		else if (setStatementTimeout && strcmp(pathKey, "maxTimeMS") == 0)
		{
			const bson_value_t *value = bson_iter_value(&cursorSpecIter);
			EnsureTopLevelFieldIsNumberLike("getMore.maxTimeMS", value);
			SetExplicitStatementTimeout(BsonValueAsInt32(value));
		}
		else if (!IsCommonSpecIgnoredField(pathKey))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("%s is an unknown field",
								   pathKey)));
		}
	}

	if (queryData->namespaceName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Required element \"collection\" missing.")));
	}

	if (cursorId == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
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
						const StringView *lookupPath,
						bool hasLet,
						pgbson **inlinedPipeline,
						pgbson **nonInlinedPipeline,
						bool *pipelineIsValid)
{
	bson_iter_t pipelineIter;
	BsonValueInitIterator(pipeline, &pipelineIter);

	bool canInline = true;
	pgbson_writer inlineWriter;
	pgbson_writer nonInlineWriter;

	PgbsonWriterInit(&inlineWriter);
	PgbsonWriterInit(&nonInlineWriter);

	pgbson_array_writer inlineArrayWriter;
	pgbson_array_writer nonInlineArrayWriter;

	PgbsonWriterStartArray(&inlineWriter, "", 0, &inlineArrayWriter);
	PgbsonWriterStartArray(&nonInlineWriter, "", 0, &nonInlineArrayWriter);
	int pipelineSize = 0;
	while (bson_iter_next(&pipelineIter))
	{
		pipelineSize++;
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIter))
		{
			canInline = false;
			*pipelineIsValid = false;
			continue;
		}

		const bson_value_t *stageValue = bson_iter_value(&pipelineIter);
		pgbsonelement stageElement;
		if (!TryGetBsonValueToPgbsonElement(stageValue, &stageElement))
		{
			canInline = false;
			*pipelineIsValid = false;
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
			canInline = false;
			*pipelineIsValid = false;
			continue;
		}

		/* Prior stage can't be inlined */
		if (!canInline)
		{
			PgbsonArrayWriterWriteValue(&nonInlineArrayWriter, stageValue);
			continue;
		}

		/* The inline function is not specified for this stage,
		 * We can't inline.
		 */
		if (definition->canInlineLookupStageFunc == NULL)
		{
			canInline = false;
			PgbsonArrayWriterWriteValue(&nonInlineArrayWriter, stageValue);
			continue;
		}

		if (!definition->canInlineLookupStageFunc(&stageElement.bsonValue, lookupPath,
												  hasLet))
		{
			canInline = false;
			PgbsonArrayWriterWriteValue(&nonInlineArrayWriter, stageValue);
			continue;
		}

		PgbsonArrayWriterWriteValue(&inlineArrayWriter, stageValue);
	}

	/*
	 * Validate number of stages for lookup nested pipeline, because after this we divide it
	 * into a set of inline and non-inlined pipelines.
	 */
	CheckMaxAllowedAggregationStages(pipelineSize);

	PgbsonWriterEndArray(&inlineWriter, &inlineArrayWriter);
	PgbsonWriterEndArray(&nonInlineWriter, &nonInlineArrayWriter);

	*pipelineIsValid = true;
	*inlinedPipeline = PgbsonWriterGetPgbson(&inlineWriter);
	*nonInlinedPipeline = PgbsonWriterGetPgbson(&nonInlineWriter);
	return canInline;
}


/*
 * Processes simple projections (dollar_project, dollar_add_fields etc).
 * This just updates the targetEntry with the new projector.
 * functionOidWithLet can be NULL if the stage doesn't support let
 */
Query *
HandleSimpleProjectionStage(const bson_value_t *existingValue, Query *query,
							AggregationPipelineBuildContext *context,
							const char *stageName, Oid functionOid,
							Oid (*functionOidWithLet)(void),
							Oid (*functionOidWithLetAndCollation)(void))
{
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40272),
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

	List *args;

	/* if valid collation is specified, we use the function with both let and collation support, if applicable */
	/* else if only variableSpec is given, we use the function with let support, if applicable */
	/* else the base function */
	bool addLetAndCollationArg = IsCollationApplicable(context->collationString) &&
								 functionOidWithLetAndCollation != NULL;
	bool addOnlyVariableSpecArg = context->variableSpec != NULL && functionOidWithLet !=
								  NULL;

	if (addLetAndCollationArg && IsClusterVersionAtleast(DocDB_V0, 102, 0))
	{
		Const *collationConst = MakeTextConst(context->collationString, strlen(
												  context->collationString));
		args = list_make4(currentProjection, addFieldsProcessed, context->variableSpec,
						  collationConst);
		functionOid = functionOidWithLetAndCollation();
	}
	else if (addOnlyVariableSpecArg)
	{
		args = list_make3(currentProjection, addFieldsProcessed,
						  context->variableSpec);
		functionOid = functionOidWithLet();
	}
	else
	{
		args = list_make2(currentProjection, addFieldsProcessed);
	}

	FuncExpr *resultExpr = makeFuncExpr(
		functionOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	firstEntry->expr = (Expr *) resultExpr;
	return query;
}


/**
 * Parses the input document for FirstN and LastN group expression operator and extracts the value for input and n.
 * @param inputDocument: input document for the $firstN operator
 * @param input:  this is a pointer which after parsing will hold array expression
 * @param elementsToFetch: this is a pointer which after parsing will hold n i.e. how many elements to fetch for result
 * @param opName: this contains the name of the operator for error msg formatting purposes. This value is supposed to be $firstN/$lastN.
 */
void
ParseInputForNGroupAccumulators(const bson_value_t *inputDocument,
								bson_value_t *input,
								bson_value_t *elementsToFetch, const char *opName)
{
	if (inputDocument->value_type != BSON_TYPE_DOCUMENT)
	{
		if (strcmp(opName, "$maxN") == 0 || strcmp(opName, "$minN") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787900),
							errmsg("specification must be an object; found %s: %s",
								   opName, BsonValueToJsonForLogging(inputDocument)),
							errdetail_log(
								"specification must be an object; opname: %s type found: %s",
								opName, BsonTypeName(inputDocument->value_type))));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787801),
							errmsg("specification must be an object; found %s :%s",
								   opName, BsonValueToJsonForLogging(inputDocument)),
							errdetail_log(
								"specification must be an object; opname: %s type found :%s",
								opName, BsonTypeName(inputDocument->value_type))));
		}
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787901),
							errmsg("%s found an unknown argument: %s", opName, key),
							errdetail_log(
								"%s found an unknown argument", opName)));
		}
	}

	/**
	 * Validation check to see if input and elements to fetch are present otherwise throw error.
	 */
	if (input->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787907),
						errmsg("%s requires an 'input' field", opName),
						errdetail_log(
							"%s requires an 'input' field", opName)));
	}

	if (elementsToFetch->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787906),
						errmsg("%s requires an 'n' field", opName),
						errdetail_log(
							"%s requires an 'n' field", opName)));
	}
}


/**
 * Parses the input document for $top(N) and $bottom(N) group expression operator and extracts the value for output, sortBy and n.
 * @param inputDocument: input document for the operator
 * @param output:  this is a pointer which after parsing will hold array expression
 * @param sortSpec:  this is a pointer which after parsing will hold the sortBy expression
 * @param elementsToFetch: this is a pointer which after parsing will hold n i.e. how many elements to fetch for result
 * @param opName: this contains the name of the operator for error msg formatting purposes. This value is supposed to be $firstN/$lastN.
 */
void
ParseInputDocumentForTopAndBottom(const bson_value_t *inputDocument, bson_value_t *output,
								  bson_value_t *elementsToFetch, bson_value_t *sortSpec,
								  const char *opName)
{
	if (inputDocument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788001),
						errmsg("specification must be an object; found %s :%s",
							   opName, BsonValueToJsonForLogging(inputDocument)),
						errdetail_log(
							"specification must be an object; opname: %s type found :%s",
							opName, BsonTypeName(inputDocument->value_type))));
	}
	bson_iter_t docIter;
	BsonValueInitIterator(inputDocument, &docIter);

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "output") == 0)
		{
			*output = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "n") == 0)
		{
			*elementsToFetch = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "sortBy") == 0)
		{
			*sortSpec = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788002),
							errmsg("Unknown argument to %s '%s'", opName, key),
							errdetail_log(
								"%s found an unknown argument", opName)));
		}
	}

	/**
	 * Validation check to see if input and elements to fetch are present otherwise throw error.
	 */
	if (elementsToFetch->value_type == BSON_TYPE_EOD && (strcmp(opName, "$topN") == 0 ||
														 strcmp(opName, "$bottomN") == 0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788003),
						errmsg("Missing value for 'n'"),
						errdetail_log(
							"%s requires an 'n' field", opName)));
	}

	if (elementsToFetch->value_type != BSON_TYPE_EOD && (strcmp(opName, "$top") == 0 ||
														 strcmp(opName, "$bottom") == 0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788002),
						errmsg("Unknown argument to %s 'n'", opName),
						errdetail_log(
							"Unknown argument to %s 'n'", opName)));
	}

	if (output->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788004),
						errmsg("Missing value for 'output'"),
						errdetail_log(
							"%s requires an 'output' field", opName)));
	}

	if (sortSpec->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788005),
						errmsg("Missing value for 'sortBy'"),
						errdetail_log(
							"%s requires a 'sortBy", opName)));
	}
	else if (sortSpec->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5788604),
						errmsg(
							"expected 'sortBy' to already be an object in the arguments to %s",
							opName),
						errdetail_log(
							"'sortBy' field in %s is not an object", opName)));
	}
}


/**
 * Parses the input document for $median and $percentile accumulator operator, extracts input, p and methods.
 * @param inputDocument: input document for the operator
 * @param input:  this is a pointer which after parsing will hold input expression
 * @param p:  this is a pointer which after parsing will hold the p, i.e. the percentile array value
 * @param method: this is a pointer which after parsing will hold method
 * @param isMedianOp: this contains the name of the operator for error msg formatting purposes. This value is supposed to be $firstN/$lastN.
 */
void
ParseInputDocumentForMedianAndPercentile(const bson_value_t *inputDocument,
										 bson_value_t *input,
										 bson_value_t *p, bson_value_t *method,
										 bool isMedianOp)
{
	const char *opName = isMedianOp ? "$median" : "$percentile";
	if (inputDocument->value_type != BSON_TYPE_DOCUMENT)
	{
		int errorcode = isMedianOp ? ERRCODE_DOCUMENTDB_LOCATION7436100 :
						ERRCODE_DOCUMENTDB_LOCATION7429703;
		ereport(ERROR, (errcode(errorcode),
						errmsg("specification must be an object; found %s type: %s",
							   opName, BsonTypeName(inputDocument->value_type)),
						errdetail_log(
							"%s specification must be an object", opName)));
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
		else if (strcmp(key, "method") == 0)
		{
			*method = *bson_iter_value(&docIter);
		}
		/* only evaluate p for $percentile */
		else if (!isMedianOp && strcmp(key, "p") == 0)
		{
			*p = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD), errmsg(
								"BSON field '$%s.%s' is an unknown field.", opName, key),
							errdetail_log("%s found an unknown argument", opName)));
		}
	}

	/* check required fields are present */
	char *keyName;
	if (((input->value_type == BSON_TYPE_EOD) && (keyName = "input")) ||
		(!isMedianOp && (p->value_type == BSON_TYPE_EOD) && (keyName = "p")) ||
		((method->value_type == BSON_TYPE_EOD) && (keyName = "method")))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414), errmsg(
							"BSON field '$%s.%s' is missing but is a required field",
							opName, keyName)));
	}

	/* validate method: can only be 'approximate' for now */
	if (method->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH), errmsg(
							"BSON field '$%s.method' is the wrong type %s, expected type 'string'",
							opName, BsonTypeName(method->value_type)),
						errdetail_log(
							"BSON field '$%s.method' expects type 'string'", opName)));
	}
	if (strcmp(method->value.v_utf8.str, "approximate") != 0)
	{
		/* Same error message for both $median and $percentile */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
							"Currently only 'approximate' can be used as percentile 'method'")));
	}

	/* set p for $median to 0.5 */
	if (isMedianOp)
	{
		p->value_type = BSON_TYPE_DOUBLE;
		p->value.v_double = 0.5;
	}
}


/**
 * This function validates and throws error in case bson type is not a numeric > 0 and less than max value of int64 i.e. 9223372036854775807
 */
void
ValidateElementForNGroupAccumulators(bson_value_t *elementsToFetch, const
									 char *opName)
{
	switch (elementsToFetch->value_type)
	{
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			if (IsBsonValueNaN(elementsToFetch))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31109),
								errmsg("Can't coerce out of range value %s to long",
									   BsonValueToJsonForLogging(elementsToFetch))));
			}

			if (IsBsonValueInfinity(elementsToFetch) != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31109),
								errmsg("Can't coerce out of range value %s to long",
									   BsonValueToJsonForLogging(elementsToFetch))));
			}

			if (!IsBsonValueFixedInteger(elementsToFetch))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787903),
								errmsg(
									"Value for 'n' must be of integral type, but found %s",
									BsonValueToJsonForLogging(elementsToFetch)),
								errdetail_log(
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787908),
								errmsg("'n' must be greater than 0, found %s",
									   BsonValueToJsonForLogging(elementsToFetch)),
								errdetail_log(
									"'n' must be greater than 0, found %ld",
									elementsToFetch->value.v_int64)));
			}
			if (elementsToFetch->value.v_int64 > 10)
			{
				if (strncmp(opName, "$lastN", 6) == 0)
				{
					ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_LASTN_GT10);
				}
				else if ((strncmp(opName, "$firstN", 7) == 0))
				{
					ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_FIRSTN_GT10);
				}
			}
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5787902),
							errmsg("Value for 'n' must be of integral type, but found %s",
								   BsonValueToJsonForLogging(elementsToFetch)),
							errdetail_log(
								"Value for 'n' must be of integral type, but found of type %s",
								BsonTypeName(elementsToFetch->value_type))));
		}
	}
}


/*
 * Given valid aggregation pipeline stages, this function returns the stage enum at the given position.
 */
Stage
GetAggregationStageAtPosition(const List *aggregationStages, int position)
{
	if (list_length(aggregationStages) <= position)
	{
		return Stage_Invalid;
	}

	AggregationStage *stage = list_nth(aggregationStages, position);
	return stage->stageDefinition->stageEnum;
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
									   BsonDollarAddFieldsFunctionOid(),
									   BsonDollarAddFieldsWithLetFunctionOid,
									   BsonDollarAddFieldsWithLetAndCollationFunctionOid);
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


/**
 * Processes the $fill Pipeine stage.
 * $fill will be rewritten to $setWindowFields and $addFields. $setWindowFields will be used to process `linear` and `locf`;
 * $addFields will be used to process const based fill. The rewritten query will be divided into two cases:
 *
 * 1) If method-fill is specified in output, the query will be rewritten with $setWindowFields.
 * 2) If only value-fill is specified in output, the query will be rewritten with $addFields, $sort.
 *
 * Example 1:
 *
 *   $fill: {
 *       partitionBy: { part : "$part"},
 *       sortBy: { key: 1 },
 *       output: {
 *           field1 : { method: "linear" },
 *           field2 : { method: "locf" },
 *           field3 : { value: "<expr>"}
 *       }
 *   }
 *
 *   ==>
 *
 *   [
 *		{
 *			$setWindowFields: {
 *				partitionBy: { part : "$part"},
 *				sortBy: { key: 1 },
 *				output: {
 *					field1: {
 *						$linearFill: "$field1"
 *					},
 *					field2: {
 *						$locf: "$field2"
 *					}
 *					field3: {
 *						$_internal_constFill: {
 *							path: "$field3",
 *							value: "<expr>"
 *						}
 *					}
 *				}
 *			}
 *		},
 *	]
 *
 * Example 2:
 *
 *  $fill: {
 *		partitionBy: { part : "$part" },
 *	    sortBy: { key : 1},
 *		output: {
 *			field1 : { value: "<expr>"}
 *		}
 *	}
 *
 *	==>
 *  [
 *      { $sort: { key : 1}},
 *      { $addFields: { field1: { $ifNull: ["$field1", "<expr>"]}}},
 *  ]
 *
 */
static Query *
HandleFill(const bson_value_t *existingValue, Query *query,
		   AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_FILL);

	bool hasSortBy = false;
	bool onlyHasValueFill = true;
	bool enableInternalWindowOperator = true;
	bson_value_t sortSpec = { 0 };
	bson_value_t addFieldsForValueFill = { 0 };

	bson_value_t setWindowFieldsSpec = { 0 };
	bson_value_t partitionByFields = { 0 };

	RewriteFillToSetWindowFieldsSpec(existingValue,
									 &hasSortBy,
									 &onlyHasValueFill,
									 &sortSpec,
									 &addFieldsForValueFill,
									 &setWindowFieldsSpec,
									 &partitionByFields);

	/* if only const based fill exists, we rewrite it into $addFields directly */
	if (onlyHasValueFill)
	{
		if (hasSortBy)
		{
			query = HandleSort(&sortSpec, query, context);
		}
		query = HandleAddFields(&addFieldsForValueFill, query, context);
		return query;
	}

	Expr *partitionByFieldsExpr = NULL;

	/* construct partitionByFieldsExpr if it exists */
	if (partitionByFields.value_type != BSON_TYPE_EOD)
	{
		/* convert partitionByFields to pgbson */
		pgbson *partitionByFieldsDoc = BsonValueToDocumentPgbson(&partitionByFields);

		TargetEntry *firstEntry = linitial(query->targetList);
		Expr *docExpr = (Expr *) firstEntry->expr;

		RangeTblEntry *rte = linitial(query->rtable);
		bool isRTEDataTable = (rte->rtekind == RTE_RELATION || rte->rtekind ==
							   RTE_FUNCTION);

		if (isRTEDataTable && IsPartitionByFieldsOnShardKey(partitionByFieldsDoc,
															context->mongoCollection))
		{
			partitionByFieldsExpr = (Expr *) makeVar(((Var *) docExpr)->varno,
													 DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
													 INT8OID, -1,
													 InvalidOid, 0);
		}
		else
		{
			Const *partitionConst = MakeBsonConst(partitionByFieldsDoc);
			partitionByFieldsExpr = (Expr *) makeFuncExpr(
				BsonExpressionPartitionByFieldsGetFunctionOid(),
				BsonTypeId(), list_make2(
					docExpr, partitionConst),
				InvalidOid, InvalidOid,
				COERCE_EXPLICIT_CALL);
		}
	}

	query = HandleSetWindowFieldsCore(&setWindowFieldsSpec, query, context,
									  partitionByFieldsExpr,
									  enableInternalWindowOperator);

	return query;
}


static void
RewriteFillToSetWindowFieldsSpec(const bson_value_t *fillSpec,
								 bool *hasSortBy,
								 bool *onlyHasValueFill,
								 bson_value_t *sortSpec,
								 bson_value_t *addFieldsForValueFill,
								 bson_value_t *setWindowFieldsSpec,
								 bson_value_t *partitionByFields)
{
	if (fillSpec->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40201),
						errmsg(
							"Argument to $fill stage must be an object, but found type: %s",
							BsonTypeName(fillSpec->value_type)),
						errdetail_log(
							"Argument to $fill stage must be an object, but found type: %s",
							BsonTypeName(fillSpec->value_type))));
	}


	bson_iter_t fillIter;
	BsonValueInitIterator(fillSpec, &fillIter);
	bson_value_t partitionBy = { 0 };
	bson_value_t sortBy = { 0 };
	bson_value_t output = { 0 };

	bool hasPartitionBy = false;
	bool hasPartitionByFields = false;

	while (bson_iter_next(&fillIter))
	{
		const char *key = bson_iter_key(&fillIter);
		const bson_value_t *value = bson_iter_value(&fillIter);

		if (strcmp(key, "partitionBy") == 0)
		{
			partitionBy = *value;
			hasPartitionBy = true;
		}
		else if (strcmp(key, "partitionByFields") == 0)
		{
			*partitionByFields = *value;
			hasPartitionByFields = true;
		}
		else if (strcmp(key, "sortBy") == 0)
		{
			sortBy = *value;
			*hasSortBy = true;
		}
		else if (strcmp(key, "output") == 0)
		{
			output = *value;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg("BSON field '$fill.%s' is an unknown field", key),
							errdetail_log("BSON field '$fill.%s' is an unknown field",
										  key)));
		}
	}

	if (hasPartitionBy && hasPartitionByFields)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION6050204),
						errmsg(
							"Maximum one of 'partitionBy' and 'partitionByFields can be specified in '$fill'"),
						errdetail_log(
							"Maximum one of 'partitionBy' and 'partitionByFields can be specified in '$fill'")));
	}

	/* output is a required field in $fill, check it */

	/* Required fields check */
	if (output.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg(
							"BSON field '$fill.output' is missing but a required field")));
	}


	/* validate partitionByFields spec */
	if (hasPartitionByFields)
	{
		bson_iter_t partitionByFieldsIter;
		BsonValueInitIterator(partitionByFields, &partitionByFieldsIter);

		while (bson_iter_next(&partitionByFieldsIter))
		{
			const bson_value_t *fieldBson = bson_iter_value(&partitionByFieldsIter);
			EnsureTopLevelFieldValueType("partitionByFields", fieldBson, BSON_TYPE_UTF8);
			EnsureStringValueNotDollarPrefixed(fieldBson->value.v_utf8.str,
											   fieldBson->value.v_utf8.len);
		}
	}

	pgbson_writer swfWriter;
	PgbsonWriterInit(&swfWriter);

	if (partitionBy.value_type != BSON_TYPE_EOD)
	{
		PgbsonWriterAppendValue(&swfWriter, "partitionBy", 11, &partitionBy);
	}
	if (sortBy.value_type != BSON_TYPE_EOD)
	{
		PgbsonWriterAppendValue(&swfWriter, "sortBy", 6, &sortBy);
	}

	pgbson_writer addFieldsSpecWriter;
	PgbsonWriterInit(&addFieldsSpecWriter);

	pgbson_writer outputWriter;
	PgbsonWriterStartDocument(&swfWriter, "output", 6, &outputWriter);

	bson_iter_t outputIter;
	BsonValueInitIterator(&output, &outputIter);
	while (bson_iter_next(&outputIter))
	{
		const char *fieldName = bson_iter_key(&outputIter);

		StringInfo dollarFieldName = makeStringInfo();
		appendStringInfo(dollarFieldName, "$%s", fieldName);

		const bson_value_t *fieldSpec = bson_iter_value(&outputIter);

		bson_iter_t fieldSpecIter;
		BsonValueInitIterator(fieldSpec, &fieldSpecIter);
		while (bson_iter_next(&fieldSpecIter))
		{
			const char *key = bson_iter_key(&fieldSpecIter);
			const bson_value_t *expr = bson_iter_value(&fieldSpecIter);

			pgbson_writer outputFieldSpecWriter;
			PgbsonWriterStartDocument(&outputWriter, fieldName, strlen(fieldName),
									  &outputFieldSpecWriter);
			if (strcmp(key, "method") == 0)
			{
				*onlyHasValueFill = false;
				EnsureTopLevelFieldValueType("method", expr, BSON_TYPE_UTF8);
				if (strcmp(expr->value.v_utf8.str, "linear") == 0)
				{
					PgbsonWriterAppendUtf8(&outputFieldSpecWriter, "$linearFill", 11,
										   dollarFieldName->data);
				}
				else if (strcmp(expr->value.v_utf8.str, "locf") == 0)
				{
					PgbsonWriterAppendUtf8(&outputFieldSpecWriter, "$locf", 5,
										   dollarFieldName->data);
				}
				else
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION6050202),
									errmsg("Method must be either locf or linear"),
									errdetail_log(
										"Method must be either locf or linear")));
				}
			}
			else if (strcmp(key, "value") == 0)
			{
				/**
				 * convert into output of $setWindowFields.
				 * We will do this in current iteration to avoid multiple iterations, even if it may not be used if only value fill exists.
				 */
				pgbson_writer constFillSpecWriter;
				PgbsonWriterStartDocument(&outputFieldSpecWriter, "$_internal_constFill",
										  20, &constFillSpecWriter);
				PgbsonWriterAppendUtf8(&constFillSpecWriter, "path", 4,
									   dollarFieldName->data);
				PgbsonWriterAppendValue(&constFillSpecWriter, "value", 5, expr);
				PgbsonWriterEndDocument(&outputFieldSpecWriter, &constFillSpecWriter);

				/**
				 * convert value fill to $addFields.
				 * We will do this in current iteration to avoid multiple iterations, even if it may not be used if method fill exists as well.
				 */
				pgbson_writer addFieldItemWriter;
				PgbsonWriterStartDocument(&addFieldsSpecWriter, fieldName, strlen(
											  fieldName), &addFieldItemWriter);

				pgbson_array_writer ifNullWriter;
				PgbsonWriterStartArray(&addFieldItemWriter, "$ifNull", 7, &ifNullWriter);
				PgbsonArrayWriterWriteUtf8(&ifNullWriter, dollarFieldName->data);
				PgbsonArrayWriterWriteValue(&ifNullWriter, expr);
				PgbsonWriterEndArray(&addFieldItemWriter, &ifNullWriter);

				PgbsonWriterEndDocument(&addFieldsSpecWriter, &addFieldItemWriter);
			}
			else
			{
				/* unsupported field name */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
								errmsg("BSON field '$fill.%s' is an unknown field", key),
								errdetail_log("BSON field '$fill.%s' is an unknown field",
											  key)));
			}
			PgbsonWriterEndDocument(&outputWriter, &outputFieldSpecWriter);
		}
		pfree(dollarFieldName->data);
	}

	/**
	 * construct $sort spec for sortBy if only value fill exists.
	 *
	 * Example:
	 *
	 *  $fill: {
	 *		sortBy: { key1 : 1, key2 : 1 },
	 *	}
	 *
	 *	==>
	 *
	 *  "$sort": {"sortKey": { "key1": 1, "key2": 1 }}
	 */

	if (*onlyHasValueFill && *hasSortBy)
	{
		pgbson_writer sortSpecWriter;
		PgbsonWriterInit(&sortSpecWriter);

		/* iterator each field in sortBy and write to sortKey */
		bson_iter_t sortByIter;
		BsonValueInitIterator(&sortBy, &sortByIter);
		while (bson_iter_next(&sortByIter))
		{
			const char *fieldName = bson_iter_key(&sortByIter);
			const bson_value_t *fieldValue = bson_iter_value(&sortByIter);
			PgbsonWriterAppendValue(&sortSpecWriter, fieldName, strlen(fieldName),
									fieldValue);
		}
		*sortSpec = ConvertPgbsonToBsonValue(PgbsonWriterGetPgbson(&sortSpecWriter));
	}

	PgbsonWriterEndDocument(&swfWriter, &outputWriter);

	*setWindowFieldsSpec = ConvertPgbsonToBsonValue(PgbsonWriterGetPgbson(&swfWriter));
	*addFieldsForValueFill = ConvertPgbsonToBsonValue(PgbsonWriterGetPgbson(
														  &addFieldsSpecWriter));
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
									   BsonDollarProjectFunctionOid(),
									   BsonDollarProjectWithLetFunctionOid,
									   BsonDollarProjectWithLetAndCollationFunctionOid);
}


/*
 * Mutates the query for the $redact stage
 */
static Query *
HandleRedact(const bson_value_t *existingValue, Query *query,
			 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_REDACT);

	/* Check if redact feature is available according to version. */
	if (!IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"Stage $redact is not supported yet in native pipeline"),
						errdetail_log(
							"Stage $redact is not supported yet in native pipeline")));
	}

	Const *redactSpec;
	Const *redactSpecText;

	if (existingValue->value_type == BSON_TYPE_DOCUMENT)
	{
		redactSpec = MakeBsonConst(PgbsonInitFromDocumentBsonValue(existingValue));
		redactSpecText = MakeTextConst("", 0);
	}
	else if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		redactSpec = MakeBsonConst(PgbsonInitEmpty());
		redactSpecText = MakeTextConst(
			existingValue->value.v_utf8.str, existingValue->value.v_utf8.len);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17053),
						errmsg(
							"$redact's parameter must be an expression or string valued as $$KEEP, $$DESCEND, and $$PRUNE, but input as '%s'.",
							BsonValueToJsonForLogging(existingValue)),
						errdetail_log(
							"$redact's parameter must be an expression or string valued as $$KEEP, $$DESCEND, and $$PRUNE.")));
	}

	/* The first projector is the document */
	TargetEntry *firstEntry = linitial(query->targetList);
	Expr *currentProjection = firstEntry->expr;

	/*
	 * There is two kind of $redact parameters:
	 * a. like "$redact: { $cond: { if: { $eq: ["$level", "public"] }, then: "$$KEEP", else: "$$PRUNE" }"
	 * existingValue->value_type is BSON_TYPE_DOCUMENT.
	 * BsonDollarRedactWithLetFunctionOid() takes four parameters, currentProjection, redactSpec, redactSpecText, variableSpec.
	 * redactSpec is the document and redactSpecText is empty
	 * b. like "$redact: "$$PRUNE"
	 * existingValue->value.value_type is BSON_TYPE_UTF8.
	 * In this case, BsonDollarRedactWithLetFunctionOid() takes four parameters, currentProjection, redactSpec, redactSpecText, variableSpec.
	 * redactSpec is set to empty document and redactSpecText is the string.
	 *
	 * If context->collationString is valid, we use BsonDollarRedactWithLetAndCollationFunctionOid() instead.
	 * BsonDollarRedactWithLetAndCollationFunctionOid() takes five parameters, currentProjection, redactSpec, redactSpecText, variableSpec,
	 * collationString.
	 */

	List *args = NIL;
	Oid funcOid = BsonDollarRedactWithLetFunctionOid();

	Expr *variableSpecConst = context->variableSpec == NULL ?
							  (Expr *) MakeBsonConst(PgbsonInitEmpty()) :
							  context->variableSpec;

	if (IsClusterVersionAtleast(DocDB_V0, 102, 0) &&
		IsCollationApplicable(context->collationString))
	{
		Const *collationStringConst = MakeTextConst(context->collationString, strlen(
														context->collationString));
		args = list_make5(currentProjection, redactSpec, redactSpecText,
						  variableSpecConst, collationStringConst);
		funcOid = BsonDollarRedactWithLetAndCollationFunctionOid();
	}
	else
	{
		args = list_make4(currentProjection, redactSpec, redactSpecText,
						  variableSpecConst);
	}

	FuncExpr *resultExpr = makeFuncExpr(
		funcOid, BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	firstEntry->expr = (Expr *) resultExpr;

	/* The following code addresses nested redact handling.
	 * The behavior of $lookup and $facet with nested redact differs from $unionWith:
	 * $lookup and $facet result in unexpected null values, use COALESCE to return an empty value instead.
	 * For $unionWith, it will still return null as redact does.
	 */
	if (context->nestedPipelineLevel > 0 && context->parentStageName !=
		ParentStageName_UNIONWITH)
	{
		CoalesceExpr *coalesceExpr = makeNode(CoalesceExpr);
		coalesceExpr->args = list_make2(resultExpr, MakeBsonConst(PgbsonInitEmpty()));
		coalesceExpr->coalescetype = BsonTypeId();
		coalesceExpr->coalescecollid = InvalidOid;

		firstEntry->expr = (Expr *) coalesceExpr;
	}

	return query;
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
	Oid funcOid = BsonDollarProjectFindFunctionOid();
	if (IsCollationApplicable(context->collationString) && IsClusterVersionAtleast(
			DocDB_V0, 102, 0))
	{
		pgbson *queryDoc = queryValue->value_type == BSON_TYPE_EOD ? PgbsonInitEmpty() :
						   PgbsonInitFromDocumentBsonValue(queryValue);
		Const *collationStringConst = MakeTextConst(context->collationString,
													strlen(context->collationString));

		args = list_make5(currentProjection, projectProcessed, MakeBsonConst(queryDoc),
						  context->variableSpec, collationStringConst);
		funcOid = BsonDollarProjectFindWithLetAndCollationFunctionOid();
	}
	else if (context->variableSpec != NULL)
	{
		pgbson *queryDoc = queryValue->value_type == BSON_TYPE_EOD ? PgbsonInitEmpty() :
						   PgbsonInitFromDocumentBsonValue(queryValue);
		args = list_make4(currentProjection, projectProcessed, MakeBsonConst(queryDoc),
						  context->variableSpec);
		funcOid = BsonDollarProjectFindWithLetFunctionOid();
	}
	else if (queryValue->value_type == BSON_TYPE_EOD)
	{
		args = list_make2(currentProjection, projectProcessed);
	}
	else
	{
		pgbson *queryDoc = PgbsonInitFromDocumentBsonValue(queryValue);
		Const *queryDocProcessed = MakeBsonConst(queryDoc);
		args = list_make3(currentProjection, projectProcessed, queryDocProcessed);
	}

	FuncExpr *resultExpr = makeFuncExpr(funcOid, BsonTypeId(), args, InvalidOid,
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

	Oid (*replaceRootWithLetAndCollationFuncOid)(void) =
		BsonDollarReplaceRootWithLetAndCollationFunctionOid;

	return HandleSimpleProjectionStage(existingValue, query, context, "$replaceRoot",
									   BsonDollarReplaceRootFunctionOid(),
									   &BsonDollarReplaceRootWithLetFunctionOid,
									   replaceRootWithLetAndCollationFuncOid);
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15972),
						errmsg("Argument to $skip must be a number")));
	}

	bool checkFixedInteger = true;
	if (!IsBsonValueUnquantized64BitInteger(existingValue, checkFixedInteger))
	{
		double doubleValue = BsonValueAsDouble(existingValue);
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5107200),
						errmsg(
							"invalid argument to $skip stage: Cannot represent as a 64-bit integer $skip: %f",
							doubleValue)));
	}

	int64_t skipValue = BsonValueAsInt64(existingValue);
	if (skipValue < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5107200),
						errmsg(
							"invalid argument to $skip stage: Expected a non-negative number in $skip: %ld",
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15957),
						errmsg("the limit must be specified as a number")));
	}

	bool checkFixedInteger = true;
	if (!IsBsonValue64BitInteger(existingValue, checkFixedInteger))
	{
		double doubleValue = BsonValueAsDouble(existingValue);
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5107201),
						errmsg(
							"invalid argument to $limit stage: Cannot represent as a 64-bit integer: $limit: %f",
							doubleValue)));
	}

	int64_t limitValue = BsonValueAsInt64(existingValue);
	if (limitValue < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION5107201),
						errmsg(
							"invalid argument to $skip stage: Expected a non - negative number in: $limit: %ld",
							limitValue)));
	}

	if (limitValue == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15958),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15959),
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
	filterContext.variableContext = context->variableSpec;

	if (EnableCollation)
	{
		filterContext.collationString = context->collationString;
	}

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


/*
 * Builds a change stream aggregation query in the form of
 * SELECT document, continuation FROM changestream_aggregation(args);
 */
static Query *
BuildChangeStreamFunctionQuery(Oid queryFunctionOid, List *queryArgs, bool isMultiRow)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	List *colNames = list_make2(makeString("document"), makeString("continuation"));
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_FUNCTION;
	rte->relid = InvalidOid;

	rte->eref = makeAlias("collection", colNames);
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
	rte->coltypes = list_make2_oid(BsonTypeId(), BsonTypeId());
	rte->coltypmods = list_make2_int(-1, -1);
	rte->colcollations = list_make2_oid(InvalidOid, InvalidOid);
	rte->ctename = NULL;
	rte->ctelevelsup = 0;


	Param *param = makeNode(Param);
	param->paramid = 1;
	param->paramkind = PARAM_EXTERN;
	param->paramtype = BsonTypeId();
	param->paramtypmod = -1;

	queryArgs = lappend(queryArgs, param);

	/* Now create the rtfunc*/
	FuncExpr *rangeFunc = makeFuncExpr(queryFunctionOid, RECORDOID, queryArgs,
									   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	if (isMultiRow)
	{
		rangeFunc->funcretset = true;
	}

	RangeTblFunction *rangeTableFunction = makeNode(RangeTblFunction);
	rangeTableFunction->funccolcount = 2;
	rangeTableFunction->funccolnames = colNames;
	rangeTableFunction->funccoltypes = list_make2_oid(BsonTypeId(), BsonTypeId());
	rangeTableFunction->funccoltypmods = list_make2_int(-1, -1);
	rangeTableFunction->funccolcollations = list_make2_oid(InvalidOid, InvalidOid);
	rangeTableFunction->funcparams = NULL;
	rangeTableFunction->funcexpr = (Node *) rangeFunc;

	/* Add the RTFunc to the RTE */
	rte->functions = list_make1(rangeTableFunction);

	query->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	Var *documentEntry = makeVar(1, 1, BsonTypeId(), -1, InvalidOid, 0);
	Var *continuationEntry = makeVar(1, 2, BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *documentTargetEntry = makeTargetEntry((Expr *) documentEntry, 1,
													   "document",
													   false);
	TargetEntry *continuationTargetEntry = makeTargetEntry((Expr *) continuationEntry, 2,
														   "continuation",
														   false);
	query->targetList = list_make2(documentTargetEntry, continuationTargetEntry);
	return query;
}


/*
 * Checks if the given string is in the given array of strings.
 */
static bool
StringArrayContains(const char *array[], size_t arraySize, const char *value)
{
	for (size_t i = 0; i < arraySize; i++)
	{
		if (strcmp(array[i], value) == 0)
		{
			return true;
		}
	}
	return false;
}


/*
 * Pre-checks the $changeStream pipeline stages to ensure that only supported stages are added to
 * the $changestream pipeline, also ensures that $changeStream is the first stage in the pipeline.
 * This function is called before the pipeline is mutated. It also checks if the feature is enabled.
 */
static void
PreCheckChangeStreamPipelineStages(const bson_value_t *pipelineValue,
								   const AggregationPipelineBuildContext *context)
{
	bson_iter_t pipelineIterator;
	BsonValueInitIterator(pipelineValue, &pipelineIterator);
	int stageNum = 0;
	while (bson_iter_next(&pipelineIterator))
	{
		bson_iter_t documentIterator;

		/* Any errors here will be handled in MutateQueryWithPipeline*/
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIterator) ||
			!bson_iter_recurse(&pipelineIterator, &documentIterator))
		{
			continue;
		}

		/* Any errors here will be handled in MutateQueryWithPipeline*/
		pgbsonelement stageElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&documentIterator, &stageElement))
		{
			continue;
		}

		const char *stageName = stageElement.path;

		/* The first change should be $changeStream. */
		if (stageNum == 0 && strcmp(stageName, "$changeStream") == 0)
		{
			continue;
		}
		/* Check the next stages to be one of the allowed stages. */
		else if (!StringArrayContains(CompatibleChangeStreamPipelineStages,
									  COMPATIBLE_CHANGE_STREAM_STAGES_COUNT,
									  stageName))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_ILLEGALOPERATION),
							errmsg(
								"Stage %s is not permitted in a $changeStream pipeline",
								stageName)));
		}
		stageNum++;
	}
}


/*
 * Modifies the query to handle the $changeStream stage.
 * It forma a query that calls the $changeStream function.
 * SELECT document, continuation
 * FROM changestream_aggregation(args);
 */
Query *
HandleChangeStream(const bson_value_t *existingValue, Query *query,
				   AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_CHANGE_STREAM);

	/* Check if change stream feature is available enabled by GUC. */
	if (!IsChangeStreamFeatureAvailableAndCompatible())
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"Stage $changeStream is not supported yet in native pipeline"),
						errdetail_log(
							"Stage $changeStream is not supported yet in native pipeline")));
	}

	EnsureTopLevelFieldValueType("$changeStream", existingValue, BSON_TYPE_DOCUMENT);

	if (context->mongoCollection != NULL &&
		!StringViewEqualsCString(&context->collectionNameView,
								 context->mongoCollection->name.collectionName))
	{
		/* This is a view */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTEDONVIEW),
						errmsg(
							"$changeStream is not supported on views.")));
	}

	/*Check the first stage and make sure it is $changestream. */
	if (context->stageNum != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40602),
						errmsg(
							"$changeStream is only valid as the first stage in the pipeline.")));
	}

	Const *databaseConst = makeConst(TEXTOID, -1, InvalidOid, -1,
									 context->databaseNameDatum, false, false);
	Const *collectionConst = MakeTextConst(context->collectionNameView.string,
										   context->collectionNameView.length);

	pgbson *bson = PgbsonInitFromDocumentBsonValue(existingValue);

	List *changeStreamArgs =
		list_make3(databaseConst, collectionConst, MakeBsonConst(bson));

	query = BuildChangeStreamFunctionQuery(ApiChangeStreamAggregationFunctionOid(),
										   changeStreamArgs, true);

	/* $changeStream pipeline requires a tailable cursor. */
	context->requiresTailableCursor = true;
	return query;
}


/*
 * Generates the projection functions expression for grouping stages e.g. $group, $setWindowFields
 * where the maximum number of arguments can be larger than MaxEvenFunctionArguments.
 */
Expr *
GenerateMultiExpressionRepathExpression(List *repathArgs)
{
	Assert(repathArgs != NIL && list_length(repathArgs) % 2 == 0);

	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	int numOfArguments = list_length(repathArgs);
	if (numOfArguments <= MaxEvenFunctionArguments)
	{
		return (Expr *) makeFuncExpr(BsonRepathAndBuildFunctionOid(),
									 BsonTypeId(), repathArgs, InvalidOid,
									 InvalidOid, COERCE_EXPLICIT_CALL);
	}

	/*
	 * need to create multiple bson_merge_documents(bson_repath_and_build(...), bson_repath_and_build(...)) expressions
	 * for each MaxEvenFunctionArguments number of arguments.
	 * e.g. If 220 arguments are provided then 110 group by accumulators are present in the query.
	 * which translates to:
	 * bson_dollar_merge_documents(
	 *      bson_dollar_merge_documents(
	 *          bson_repath_and_build(<50 group by exprs>),
	 *          bson_dollar_merge_documents(
	 *              bson_repath_and_build(<50 group by exprs>),
	 *              bson_repath_and_build(<10 group by exprs>)
	 *      ),
	 * )
	 */
	List *args = list_copy_head(repathArgs, MaxEvenFunctionArguments);
	List *remainingArgs = list_copy_tail(repathArgs, MaxEvenFunctionArguments);
	Expr *argsRepathExpression = (Expr *) makeFuncExpr(BsonRepathAndBuildFunctionOid(),
													   BsonTypeId(), args, InvalidOid,
													   InvalidOid, COERCE_EXPLICIT_CALL);
	Expr *remainingArgsExprs = GenerateMultiExpressionRepathExpression(remainingArgs);

	return (Expr *) makeFuncExpr(BsonDollaMergeDocumentsFunctionOid(),
								 BsonTypeId(),
								 list_make2(argsRepathExpression, remainingArgsExprs),
								 InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
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
		/* Mongo allows collation on _id field. We need to make sure we do that as well. We can't
		 * push the Id filter to primary key index if the type needs to be collation aware (e.g., _id contains UTF8 )*/
		bool isCollationAware;
		bool isPointRead = false;
		Expr *idFilter = CreateIdFilterForQuery(existingQuals, var->varno,
												&isCollationAware, &isPointRead);

		if (idFilter != NULL &&
			!(isCollationAware && IsCollationApplicable(context->collationString)))
		{
			existingQuals = lappend(existingQuals, idFilter);
			context->isPointReadQuery = isPointRead;
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31002),
						errmsg("$unset specification must be a string or an array")));
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		if (existingValue->value.v_utf8.len == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40352),
							errmsg("FieldPath cannot be constructed with empty string")));
		}

		if (existingValue->value.v_utf8.str[0] == '$')
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16410),
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31120),
								errmsg(
									"$unset specification must be a string or an array containing only string values")));
			}

			if (arrayValue->value.v_utf8.len == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40352),
								errmsg(
									"FieldPath cannot be constructed with empty string")));
			}

			if (arrayValue->value.v_utf8.str[0] == '$')
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16410),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31119),
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
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28810),
										errmsg(
											"expected a non-empty string for the includeArrayIndex option to $unwind stage")));
					}

					StringView includeArrayIndexView = (StringView) {
						.string = value->value.v_utf8.str,
						.length = value->value.v_utf8.len
					};

					if (includeArrayIndexView.length == 0)
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28810),
										errmsg(
											"expected a non-empty string for the includeArrayIndex option to $unwind stage")));
					}

					if (StringViewStartsWith(&includeArrayIndexView, '$'))
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28822),
										errmsg(
											"includeArrayIndex option to $unwind stage should not be prefixed with a '$': %s",
											includeArrayIndexView.string)));
					}
				}
				else if (strcmp(key, "preserveNullAndEmptyArrays") == 0)
				{
					if (value->value_type != BSON_TYPE_BOOL)
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28809),
										errmsg(
											"expected a boolean for the preserveNullAndEmptyArrays option to $unwind stage")));
					}
				}
				else
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28811),
									errmsg("unrecognized option to $unwind stage")));
				}
			}

			break;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15981),
							errmsg(
								"expected either a string or an object as specification for $unwind stage, got %s",
								BsonTypeName(existingValue->value_type))));
		}
	}

	if (pathValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28812),
						errmsg("No path specified to $unwind stage")));
	}
	if (pathValue.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28808),
						errmsg("Expected a string as the path for $unwind stage, got %s",
							   BsonTypeName(pathValue.value_type))));
	}

	StringView pathView = {
		.string = pathValue.value.v_utf8.str,
		.length = pathValue.value.v_utf8.len
	};
	if (pathView.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28812),
						errmsg("No path specified to $unwind stage")));
	}

	if (!StringViewStartsWith(&pathView, '$') || pathView.length == 1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28818),
						errmsg(
							"path option to $unwind stage should be prefixed with a '$': %.*s",
							pathView.length, pathView.string)));
	}

	if (pathView.string[1] == '$')
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16410),
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

	if (context->stageNum != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40603),
						errmsg(
							"$geoNear was not the first stage in the pipeline.")));
	}


	RangeTblEntry *rte = linitial(query->rtable);
	if (rte->rtekind != RTE_RELATION)
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg("$geoNear is only supported on collections."),
					errdetail_log(
						"$geoNear is only supported on collections. RTE KIND: %d",
						rte->rtekind)));
	}

	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_LOCATION10065),
					errmsg("invalid parameter: expected an object ($geoNear)")));
	}

	pgbson *geoNearQueryDoc = EvaluateGeoNearConstExpression(existingValue,
															 context->variableSpec);

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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40156),
						errmsg("the count field must be a non-empty string")));
	}

	if (StringViewStartsWith(&countField, '$'))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40158),
						errmsg("the count field cannot be a $-prefixed path")));
	}

	if (StringViewContains(&countField, '.'))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40160),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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
	bool isNaturalReverseSort = false;
	int naturalCount = 0;
	int nonNaturalCount = 0;

	while (bson_iter_next(&sortSpec))
	{
		pgbsonelement element;
		BsonIterToPgbsonElement(&sortSpec, &element);

		if (strcmp(element.path, "$natural") == 0)
		{
			if (!BsonValueIsNumber(&element.bsonValue))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Invalid sort direction %s",
									   BsonValueToJsonForLogging(
										   &element.bsonValue))));
			}

			int64_t naturalValue = BsonValueAsInt64(&element.bsonValue);

			isNaturalSort = false;
			isNaturalReverseSort = false;

			if (naturalValue == 1)
			{
				isNaturalSort = true;
			}
			else if (naturalValue == -1)
			{
				isNaturalReverseSort = true;
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$natural sort cannot be set to a value other than -1 or 1.")));
			}
			naturalCount++;
		}
		else
		{
			nonNaturalCount++;
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
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg("Invalid sort direction %s",
										   BsonValueToJsonForLogging(
											   &element.bsonValue))));
				}
			}

			Expr *expr = NULL;
			pgbson *sortDoc = PgbsonElementToPgbson(&element);
			Const *sortBson = MakeBsonConst(sortDoc);
			bool isAscending = ValidateOrderbyExpressionAndGetIsAscending(sortDoc);
			SortByNulls sortByNulls = SORTBY_NULLS_DEFAULT;

			bool hasSortById = strcmp(element.path, "_id") == 0;
			bool canPushdownSortById = false;

			if (hasSortById)
			{
				ReportFeatureUsage(FEATURE_STAGE_SORT_BY_ID);

				if (CanSortByObjectId(query, context))
				{
					canPushdownSortById = true;
					ReportFeatureUsage(FEATURE_STAGE_SORT_BY_ID_PUSHDOWNABLE);
				}
			}

			/* If the sort is on _id and we can push it down to the primary key index, use ORDER BY object_id instead. */
			if (EnableSortbyIdPushDownToPrimaryKey && canPushdownSortById)
			{
				expr = (Expr *) makeVar(1,
										DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER,
										BsonTypeId(), -1, InvalidOid, 0);
			}
			else
			{
				/* match mongo behavior. */
				sortByNulls = isAscending ? SORTBY_NULLS_FIRST : SORTBY_NULLS_LAST;
				expr = (Expr *) makeFuncExpr(BsonOrderByFunctionOid(),
											 BsonTypeId(),
											 list_make2(sortInput, sortBson),
											 InvalidOid, InvalidOid,
											 COERCE_EXPLICIT_CALL);
			}

			SortBy *sortBy = makeNode(SortBy);
			sortBy->location = -1;
			sortBy->sortby_dir = isAscending ? SORTBY_ASC : SORTBY_DESC;
			sortBy->sortby_nulls = sortByNulls;
			sortBy->node = (Node *) expr;

			bool resjunk = true;
			TargetEntry *sortEntry = makeTargetEntry((Expr *) expr,
													 (AttrNumber) parseState->p_next_resno
													 ++,
													 "?sort?",
													 resjunk);
			targetEntryList = lappend(targetEntryList, sortEntry);
			sortlist = addTargetToSortList(parseState, sortEntry,
										   sortlist, targetEntryList, sortBy);
		}
	}

	/* if there is other operator exist with $natural, should throw exception, example like db.coll.find().sort({$size:1, $natural: -1}) */
	if (naturalCount > 0 && nonNaturalCount > 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"$natural sort cannot be set to a value other than -1 or 1.")));
	}

	if (isNaturalReverseSort || isNaturalSort)
	{
		/*server would throw exception Exception while reading from stream if collection is null, directly return query when collection is null*/
		if (context->mongoCollection == NULL)
		{
			return query;
		}

		Var *ctid_var = makeVar(1, SelfItemPointerAttributeNumber,
								TIDOID, -1, 0, 0);
		TargetEntry *tle_ctid = makeTargetEntry((Expr *) ctid_var,
												(AttrNumber) parseState->p_next_resno++,
												"?ctid?",
												true);

		SortBy *sortBy = makeNode(SortBy);
		sortBy->location = -1;
		sortBy->sortby_dir = isNaturalSort ? SORTBY_ASC : SORTBY_DESC;
		sortBy->sortby_nulls = isNaturalSort ? SORTBY_NULLS_FIRST : SORTBY_NULLS_LAST;
		sortBy->node = (Node *) ctid_var;

		targetEntryList = lappend(targetEntryList, tle_ctid);
		sortlist = addTargetToSortList(parseState, tle_ctid,
									   sortlist, targetEntryList, sortBy);

		query->targetList = targetEntryList;
		query->sortClause = sortlist;
		pfree(parseState);
		return query;
	}

	pfree(parseState);
	if (sortlist == NIL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15976),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40147),
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


inline static bool
CanSortByObjectId(Query *query, AggregationPipelineBuildContext *context)
{
	RangeTblEntry *rte = linitial(query->rtable);
	if (context->mongoCollection == NULL ||
		rte->rtekind != RTE_RELATION ||
		rte->relid != context->mongoCollection->relationId)
	{
		/* We're not querying the base table. */
		return false;
	}

	void *walkerState = NULL;
	return context->mongoCollection->shardKey == NULL ||
		   expression_tree_walker(query->jointree->quals, HasShardKeyFilterWalker,
								  walkerState);
}


static bool
HasShardKeyFilterWalker(Node *node, void *state)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, OpExpr))
	{
		OpExpr *opExpr = (OpExpr *) node;
		if (opExpr->opno != BigintEqualOperatorId())
		{
			return false;
		}

		/* We always construct filter as shard_key_value = <const> */
		Expr *leftOperator = linitial(opExpr->args);
		if (!IsA(leftOperator, Var))
		{
			return false;
		}

		return ((Var *) leftOperator)->varattno ==
			   DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER;
	}

	return expression_tree_walker(node, HasShardKeyFilterWalker, state);
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


/*
 * Given an accumulator expression, checks whether it can be statically
 * evaluated to a const. If it can, then returns a const "Expr".
 * If not, returns the original docExpr.
 */
static Expr *
GetDocumentExprForGroupAccumulatorValue(const bson_value_t *accumulatorValue,
										Expr *docExpr)
{
	if (!EnableSimplifyGroupAccumulators)
	{
		return docExpr;
	}

	ParseAggregationExpressionContext parseContext = { 0 };
	AggregationExpressionData expressionData;
	memset(&expressionData, 0, sizeof(AggregationExpressionData));

	ParseAggregationExpressionData(&expressionData, accumulatorValue, &parseContext);
	if (expressionData.kind == AggregationExpressionKind_Constant)
	{
		/* Expression evaluates to a const, we don't need to pass in the input documentExpr */
		return (Expr *) MakeBsonConst(PgbsonInitEmpty());
	}

	return docExpr;
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
						  Expr *documentExpr, Oid aggregateFunctionOid,
						  Expr *variableSpec)
{
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  accumulatorValue));

	documentExpr = GetDocumentExprForGroupAccumulatorValue(accumulatorValue,
														   documentExpr);
	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs;
	Oid functionId;
	if (variableSpec != NULL)
	{
		groupArgs = list_make4(documentExpr, constValue, trueConst, variableSpec);
		functionId = BsonExpressionGetWithLetFunctionOid();
	}
	else
	{
		groupArgs = list_make3(documentExpr, constValue, trueConst);
		functionId = BsonExpressionGetFunctionOid();
	}


	FuncExpr *accumFunc = makeFuncExpr(
		functionId, BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	if (BsonTypeId() != DocumentDBCoreBsonTypeId() &&
		IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		accumFunc = makeFuncExpr(
			DocumentDBCoreBsonToBsonFunctionOId(), BsonTypeId(), list_make1(accumFunc),
			InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
	}

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
							char *fieldPath, bool handleSingleValue,
							Expr *variableSpec)
{
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
												  accumulatorValue));

	/* isNullOnEmpty is set to false for $push */
	Const *isNullOnEmptyConst = (Const *) MakeBoolValueConst(false);
	List *funcArgs;
	Oid functionOid;


	if (variableSpec != NULL)
	{
		funcArgs = list_make4(documentExpr, constValue, isNullOnEmptyConst, variableSpec);
		functionOid = BsonExpressionGetWithLetFunctionOid();
	}
	else
	{
		funcArgs = list_make3(documentExpr, constValue, isNullOnEmptyConst);
		functionOid = BsonExpressionGetFunctionOid();
	}

	FuncExpr *accumFunc = makeFuncExpr(functionOid, BsonTypeId(), funcArgs, InvalidOid,
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
								Expr *documentExpr, const bson_value_t *sortSpec,
								Expr *variableSpec)
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

	Const *sortArrayConst = makeConst(GetBsonArrayTypeOid(), -1, InvalidOid, -1,
									  PointerGetDatum(arrayValue), false, false);


	if (variableSpec != NULL)
	{
		/* If variableSpec only contains the time system variables, do not fail. */
		Node *specNode = (Node *) variableSpec;
		Const *specConst = (Const *) specNode;
		pgbson *specBson = DatumGetPgBson(specConst->constvalue);

		bson_iter_t iter;
		if (PgbsonInitIteratorAtPath(specBson, "let", &iter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("let with $mergeObjects is not supported yet")));
		}
	}

	/*
	 * Here we add a parameter with the input expression. The reason is we need
	 * to evaluate it against the document after the sort takes place.
	 */
	Expr *inputExpression = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(
													   accumulatorValue));
	Aggref *aggref = CreateMultiArgAggregate(BsonMergeObjectsFunctionOid(),
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
AddSimpleNGroupAccumulator(Query *query, const bson_value_t *input,
						   bson_value_t *elementsToFetch,
						   List *repathArgs, Const *accumulatorText,
						   ParseState *parseState, char *identifiers,
						   Expr *documentExpr, Oid aggregateFunctionOid,
						   StringView *accumulatorName, Expr *variableSpec)
{
	ValidateElementForNGroupAccumulators(elementsToFetch, accumulatorName->string);

	/* First add the agg function */
	Const *nConst = makeConst(INT8OID, -1, InvalidOid, sizeof(int64_t),
							  Int64GetDatum(elementsToFetch->value.v_int64), false, true);
	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid,
											 list_make2((Expr *) documentExpr, nConst),
											 list_make2_oid(BsonTypeId(),
															nConst->consttype),
											 parseState);

	/* First apply the map function to the document */
	Const *fieldConst = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(""),
								  false,
								  false);
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(input));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs;
	Oid functionOid;

	if (variableSpec != NULL)
	{
		functionOid = BsonExpressionMapWithLetFunctionOid();
		groupArgs = list_make5(aggref, fieldConst, constValue, trueConst,
							   variableSpec);
	}
	else
	{
		functionOid = BsonExpressionMapFunctionOid();
		groupArgs = list_make4(aggref, fieldConst, constValue, trueConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), groupArgs, InvalidOid,
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
						  const bson_value_t *sortSpec,
						  Expr *variableSpec)
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

	Const *sortArrayConst = makeConst(GetBsonArrayTypeOid(), -1, InvalidOid, -1,
									  PointerGetDatum(arrayValue), false, false);

	/* Cast documentExpr from CoreSchemaName.bson to bson to ensure type */
	/* correctness for accumulators that require bson. */
	if (BsonTypeId() != DocumentDBCoreBsonTypeId())
	{
		documentExpr = (Expr *) makeRelabelType(documentExpr, BsonTypeId(), -1,
												InvalidOid,
												COERCE_IMPLICIT_CAST);
	}

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
	List *groupArgs;
	Oid expressionGetFunction;

	if (variableSpec != NULL)
	{
		groupArgs = list_make4(aggref, constValue, trueConst, variableSpec);
		expressionGetFunction = BsonExpressionGetWithLetFunctionOid();
	}
	else
	{
		groupArgs = list_make3(aggref, constValue, trueConst);
		expressionGetFunction = BsonExpressionGetFunctionOid();
	}

	FuncExpr *accumFunc = makeFuncExpr(
		expressionGetFunction, BsonTypeId(), groupArgs, InvalidOid,
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
AddSortedNGroupAccumulator(Query *query, const bson_value_t *input,
						   bson_value_t *elementsToFetch,
						   List *repathArgs, Const *accumulatorText,
						   ParseState *parseState, char *identifiers,
						   Expr *documentExpr, Oid aggregateFunctionOid,
						   const bson_value_t *sortSpec, StringView *accumulatorName,
						   Expr *variableSpec)
{
	ValidateElementForNGroupAccumulators(elementsToFetch, accumulatorName->string);


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
							  Int64GetDatum(elementsToFetch->value.v_int64), false, true);
	ArrayType *arrayValue = construct_array(sortDatumArray, nelems, BsonTypeId(), -1,
											false, TYPALIGN_INT);
	Const *sortArrayConst = makeConst(GetBsonArrayTypeOid(), -1, InvalidOid, -1,
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
	Expr *constValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(input));

	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	List *groupArgs;
	Oid functionOid;

	if (variableSpec != NULL)
	{
		functionOid = BsonExpressionMapWithLetFunctionOid();
		groupArgs = list_make5(aggref, fieldConst, constValue, trueConst, variableSpec);
	}
	else
	{
		functionOid = BsonExpressionMapFunctionOid();
		groupArgs = list_make4(aggref, fieldConst, constValue, trueConst);
	}

	FuncExpr *accumFunc = makeFuncExpr(
		functionOid, BsonTypeId(), groupArgs, InvalidOid,
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
 * Function used to support maxN/minN accumulator.
 */
inline static List *
AddMaxMinNGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
						   List *repathArgs, Const *accumulatorText,
						   ParseState *parseState, char *identifiers,
						   Expr *documentExpr, Oid aggregateFunctionOid,
						   StringView *accumulatorName,
						   Expr *variableSpec)
{
	bson_value_t input = { 0 };
	bson_value_t elementsToFetch = { 0 };

	/*check the syntax of maxN/minN */
	ParseInputForNGroupAccumulators(accumulatorValue, &input, &elementsToFetch,
									accumulatorName->string);

	return AddSimpleGroupAccumulator(query, accumulatorValue, repathArgs, accumulatorText,
									 parseState, identifiers, documentExpr,
									 aggregateFunctionOid, variableSpec);
}


/*
 * Function used to support percentile/median accumulator.
 */
inline static List *
AddPercentileMedianGroupAccumulator(Query *query, const bson_value_t *accumulatorValue,
									List *repathArgs, Const *accumulatorText,
									ParseState *parseState, char *identifiers,
									Expr *documentExpr, StringView *accumulatorName,
									Expr *variableSpec, bool isMedianOp)
{
	bson_value_t input = { 0 };
	bson_value_t p = { 0 };
	bson_value_t method = { 0 };

	ParseInputDocumentForMedianAndPercentile(accumulatorValue, &input, &p, &method,
											 isMedianOp);

	/* construct expression to get input and p */
	List *inputFuncArgs;
	List *pFuncArgs;
	Oid bsonExpressionGetFunction;

	Expr *inputConstValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&input));
	Const *accuracyConstValue = makeConst(INT4OID, -1, InvalidOid, sizeof(int32_t),
										  Int32GetDatum(TdigestCompressionAccuracy),
										  false, true);
	Expr *pConstValue = (Expr *) MakeBsonConst(BsonValueToDocumentPgbson(&p));
	Const *trueConst = makeConst(BOOLOID, -1, InvalidOid, 1, BoolGetDatum(true), false,
								 true);
	if (variableSpec != NULL)
	{
		bsonExpressionGetFunction = BsonExpressionGetWithLetFunctionOid();
		inputFuncArgs = list_make4(documentExpr, inputConstValue, trueConst,
								   variableSpec);
		pFuncArgs = list_make4(documentExpr, pConstValue, trueConst, variableSpec);
	}
	else
	{
		bsonExpressionGetFunction = BsonExpressionGetFunctionOid();
		inputFuncArgs = list_make3(documentExpr, inputConstValue, trueConst);
		pFuncArgs = list_make3(documentExpr, pConstValue, trueConst);
	}

	FuncExpr *inputAccumFunc = makeFuncExpr(bsonExpressionGetFunction, BsonTypeId(),
											inputFuncArgs, InvalidOid,
											InvalidOid, COERCE_EXPLICIT_CALL);
	FuncExpr *pAccumFunc = makeFuncExpr(bsonExpressionGetFunction, BsonTypeId(),
										pFuncArgs, InvalidOid,
										InvalidOid, COERCE_EXPLICIT_CALL);

	if (BsonTypeId() != DocumentDBCoreBsonTypeId() &&
		IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		inputAccumFunc = makeFuncExpr(
			DocumentDBCoreBsonToBsonFunctionOId(), BsonTypeId(), list_make1(
				inputAccumFunc),
			InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
		pAccumFunc = makeFuncExpr(
			DocumentDBCoreBsonToBsonFunctionOId(), BsonTypeId(), list_make1(pAccumFunc),
			InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
	}

	Oid aggregateFunctionOid = isMedianOp ? BsonMedianAggregateFunctionOid() :
							   BsonPercentileAggregateFunctionOid();
	Aggref *aggref = CreateMultiArgAggregate(aggregateFunctionOid, list_make3(
												 (Expr *) inputAccumFunc,
												 accuracyConstValue, (Expr *) pAccumFunc),
											 list_make3_oid(
												 BsonTypeId(),
												 accuracyConstValue->consttype,
												 BsonTypeId()), parseState);

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) accumulatorText,
														parseState, identifiers, query,
														TEXTOID, NULL));

	repathArgs = lappend(repathArgs, AddGroupExpression((Expr *) aggref, parseState,
														identifiers, query, BsonTypeId(),
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
 * due to a quirk in the distribution layer's worker query generation.
 *
 * TODO: Support n(elementsToFetch) as an expression for firstN, lastN, topN, bottomN
 */
Query *
HandleGroup(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_GROUP);

	/* Part 1, let's do the group */
	if (existingValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15947),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15955),
						errmsg("a group specification must include an _id")));
	}

	pgbson *groupValue = BsonValueToDocumentPgbson(&idValue);


	ParseState *parseState = make_parsestate(NULL);
	parseState->p_next_resno = 1;
	parseState->p_expr_kind = EXPR_KIND_GROUP_BY;

	List *groupArgs;
	Oid bsonExpressionGetFunction;
	Expr *groupIdDocumentExpr = GetDocumentExprForGroupAccumulatorValue(&idValue,
																		origEntry->expr);
	if (context->variableSpec != NULL)
	{
		bsonExpressionGetFunction = BsonExpressionGetWithLetFunctionOid();
		groupArgs = list_make4(groupIdDocumentExpr, MakeBsonConst(groupValue),
							   MakeBoolValueConst(true), context->variableSpec);
	}
	else
	{
		bsonExpressionGetFunction = BsonExpressionGetFunctionOid();
		groupArgs = list_make3(groupIdDocumentExpr, MakeBsonConst(groupValue),
							   MakeBoolValueConst(true));
	}

	FuncExpr *groupFunc = makeFuncExpr(
		bsonExpressionGetFunction, BsonTypeId(), groupArgs, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	if (BsonTypeId() != DocumentDBCoreBsonTypeId() &&
		IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		groupFunc = makeFuncExpr(
			DocumentDBCoreBsonToBsonFunctionOId(), BsonTypeId(), list_make1(groupFunc),
			InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
	}

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

		if (StringViewContains(&keyView, '.'))
		{
			/* Paths here cannot be dotted paths */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40235),
							errmsg("The field name %.*s cannot contain '.'",
								   keyView.length, keyView.string)));
		}

		Const *accumulatorText = MakeTextConst(keyView.string, keyView.length);

		bson_iter_t accumulatorIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&groupIter) ||
			!bson_iter_recurse(&groupIter, &accumulatorIterator))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40234),
							errmsg("The field '%.*s' must be an accumulator object",
								   keyView.length, keyView.string)));
		}

		pgbsonelement accumulatorElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&accumulatorIterator,
													   &accumulatorElement))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40238),
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
												   BsonAvgAggregateFunctionOid(),
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$sum"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonSumAggregateFunctionOid(),
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$max"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonMaxAggregateFunctionOid(),
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$min"))
		{
			repathArgs = AddSimpleGroupAccumulator(query, &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonMinAggregateFunctionOid(),
												   context->variableSpec);
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
												   BsonSumAggregateFunctionOid(),
												   context->variableSpec);
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
													   BsonFirstOnSortedAggregateFunctionOid(),
													   context->variableSpec);
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
													   &context->sortSpec,
													   context->variableSpec);
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
													   BsonLastOnSortedAggregateFunctionOid(),
													   context->variableSpec);
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
													   &context->sortSpec,
													   context->variableSpec);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$firstN"))
		{
			ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_FIRSTN);
			bson_value_t input = { 0 };
			bson_value_t elementsToFetch = { 0 };
			ParseInputForNGroupAccumulators(&accumulatorElement.bsonValue, &input,
											&elementsToFetch, accumulatorName.string);
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleNGroupAccumulator(query,
														&input,
														&elementsToFetch,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonFirstNOnSortedAggregateFunctionOid(),
														&accumulatorName,
														context->variableSpec);
			}
			else
			{
				repathArgs = AddSortedNGroupAccumulator(query,
														&input,
														&elementsToFetch,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonFirstNAggregateFunctionOid(),
														&context->sortSpec,
														&accumulatorName,
														context->variableSpec);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$lastN"))
		{
			ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_LASTN);
			bson_value_t input = { 0 };
			bson_value_t elementsToFetch = { 0 };
			ParseInputForNGroupAccumulators(&accumulatorElement.bsonValue, &input,
											&elementsToFetch, accumulatorName.string);
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleNGroupAccumulator(query,
														&input,
														&elementsToFetch,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonLastNOnSortedAggregateFunctionOid(),
														&accumulatorName,
														context->variableSpec);
			}
			else
			{
				repathArgs = AddSortedNGroupAccumulator(query,
														&input,
														&elementsToFetch,
														repathArgs,
														accumulatorText, parseState,
														identifiers,
														origEntry->expr,
														BsonLastNAggregateFunctionOid(),
														&context->sortSpec,
														&accumulatorName,
														context->variableSpec);
			}
		}
		else if (StringViewEqualsCString(&accumulatorName, "$maxN"))
		{
			repathArgs = AddMaxMinNGroupAccumulator(query,
													&accumulatorElement.bsonValue,
													repathArgs,
													accumulatorText, parseState,
													identifiers,
													origEntry->expr,
													BsonMaxNAggregateFunctionOid(),
													&accumulatorName,
													context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$minN"))
		{
			repathArgs = AddMaxMinNGroupAccumulator(query,
													&accumulatorElement.bsonValue,
													repathArgs,
													accumulatorText, parseState,
													identifiers,
													origEntry->expr,
													BsonMinNAggregateFunctionOid(),
													&accumulatorName,
													context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$addToSet"))
		{
			repathArgs = AddSimpleGroupAccumulator(query,
												   &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonAddToSetAggregateFunctionOid(),
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$mergeObjects"))
		{
			if (context->sortSpec.value_type == BSON_TYPE_EOD)
			{
				repathArgs = AddSimpleGroupAccumulator(query,
													   &accumulatorElement.bsonValue,
													   repathArgs,
													   accumulatorText, parseState,
													   identifiers,
													   origEntry->expr,
													   BsonMergeObjectsOnSortedFunctionOid(),
													   context->variableSpec);
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
															 &context->sortSpec,
															 context->variableSpec);
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
													 handleSingleValue,
													 context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$stdDevSamp"))
		{
			if (accumulatorElement.bsonValue.value_type == BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40237), errmsg(
									"The %s accumulator is a unary operator",
									accumulatorName.string)),
						errdetail_log("The %s accumulator is a unary operator",
									  accumulatorName.string));
			}

			repathArgs = AddSimpleGroupAccumulator(query,
												   &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonStdDevSampAggregateFunctionOid(),
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$stdDevPop"))
		{
			if (accumulatorElement.bsonValue.value_type == BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40237), errmsg(
									"The %s accumulator is a unary operator",
									accumulatorName.string)),
						errdetail_log("The %s accumulator is a unary operator",
									  accumulatorName.string));
			}

			repathArgs = AddSimpleGroupAccumulator(query,
												   &accumulatorElement.bsonValue,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonStdDevPopAggregateFunctionOid(),
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$top"))
		{
			bson_value_t output = { 0 };
			bson_value_t elementsToFetch = { 0 };
			bson_value_t sortSpec = { 0 };
			ParseInputDocumentForTopAndBottom(&accumulatorElement.bsonValue, &output,
											  &elementsToFetch,
											  &sortSpec,
											  accumulatorName.string);
			repathArgs = AddSortedGroupAccumulator(query,
												   &output,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonFirstAggregateFunctionOid(),
												   &sortSpec,
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$bottom"))
		{
			bson_value_t output = { 0 };
			bson_value_t elementsToFetch = { 0 };
			bson_value_t sortSpec = { 0 };
			ParseInputDocumentForTopAndBottom(&accumulatorElement.bsonValue, &output,
											  &elementsToFetch,
											  &sortSpec,
											  accumulatorName.string);
			repathArgs = AddSortedGroupAccumulator(query,
												   &output,
												   repathArgs,
												   accumulatorText, parseState,
												   identifiers,
												   origEntry->expr,
												   BsonLastAggregateFunctionOid(),
												   &sortSpec,
												   context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$topN"))
		{
			ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_TOPN);

			/* Parse accumulatorValue to pull output, n and sortBy*/
			bson_value_t input = { 0 };
			bson_value_t elementsToFetch = { 0 };
			bson_value_t sortSpec = { 0 };

			ParseInputDocumentForTopAndBottom(&accumulatorElement.bsonValue, &input,
											  &elementsToFetch, &sortSpec,
											  accumulatorName.string);
			repathArgs = AddSortedNGroupAccumulator(query,
													&input,
													&elementsToFetch,
													repathArgs,
													accumulatorText, parseState,
													identifiers,
													origEntry->expr,
													BsonFirstNAggregateFunctionOid(),
													&sortSpec,
													&accumulatorName,
													context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$bottomN"))
		{
			ReportFeatureUsage(FEATURE_STAGE_GROUP_ACC_BOTTOMN);

			/* Parse accumulatorValue to pull output, n and sortBy*/
			bson_value_t input = { 0 };
			bson_value_t elementsToFetch = { 0 };
			bson_value_t sortSpec = { 0 };

			ParseInputDocumentForTopAndBottom(&accumulatorElement.bsonValue, &input,
											  &elementsToFetch, &sortSpec,
											  accumulatorName.string);
			repathArgs = AddSortedNGroupAccumulator(query,
													&input,
													&elementsToFetch,
													repathArgs,
													accumulatorText, parseState,
													identifiers,
													origEntry->expr,
													BsonLastNAggregateFunctionOid(),
													&sortSpec,
													&accumulatorName,
													context->variableSpec);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$median"))
		{
			if (!(IsClusterVersionAtleast(DocDB_V0, 24, 0)))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg("Accumulator $median is not supported yet")));
			}
			repathArgs = AddPercentileMedianGroupAccumulator(query,
															 &accumulatorElement.bsonValue,
															 repathArgs,
															 accumulatorText, parseState,
															 identifiers,
															 origEntry->expr,
															 &accumulatorName,
															 context->variableSpec, true);
		}
		else if (StringViewEqualsCString(&accumulatorName, "$percentile"))
		{
			if (!(IsClusterVersionAtleast(DocDB_V0, 24, 0)))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg(
									"Accumulator $percentile is not supported yet")));
			}
			repathArgs = AddPercentileMedianGroupAccumulator(query,
															 &accumulatorElement.bsonValue,
															 repathArgs,
															 accumulatorText, parseState,
															 identifiers,
															 origEntry->expr,
															 &accumulatorName,
															 context->variableSpec,
															 false);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15952),
							errmsg("Unknown group operator %s",
								   accumulatorElement.path),
							errdetail_log("Unknown group operator %s",
										  accumulatorElement.path)));
		}
	}

	/* Assign the _id clause as what we're grouping on */
	SortGroupClause *grpcl = makeNode(SortGroupClause);
	grpcl->tleSortGroupRef = assignSortGroupRef(groupEntry, query->targetList);
	grpcl->eqop = BsonEqualOperatorId();
	grpcl->sortop = BsonLessThanOperatorId();
	grpcl->nulls_first = false; /* OK with or without sortop */
	grpcl->hashable = true;
	query->groupClause = list_make1(grpcl);

	/* Now that the group + accumulators are done, push to a subquery
	 * Request preserving the N-entry T-list
	 */
	context->expandTargetList = true;
	query = MigrateQueryToSubQuery(query, context);

	/* Take the output and replace it with the repath_and_build */
	TargetEntry *entry = linitial(query->targetList);
	Expr *repathExpression = GenerateMultiExpressionRepathExpression(repathArgs);

	entry->expr = repathExpression;
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

			/* append only non-junk columns */
			if (!tle->resjunk)
			{
				colnames = lappend(colnames, makeString(tle->resname ? tle->resname :
														""));
			}
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
 * Checks if partitionByFields expression of $densify stage is on the shard key
 * of the collection
 *
 * `partitionByFields`: { "": ["a", "b", "c"]}
 * `shardkey`: {"a": "hashed", "b": "hashed", "c": "hashed"}
 *
 * These 2 are same
 */
bool
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
								 List **pipelineStages,
								 AggregationPipelineBuildContext *context)
{
	int viewDepth = 0;
	bool canViewUseShardTable = context->allowShardBaseTable;
	while (true)
	{
		CHECK_FOR_INTERRUPTS();
		check_stack_depth();

		if (viewDepth > MAX_VIEW_DEPTH)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_VIEWDEPTHLIMITEXCEEDED),
							errmsg("View depth exceeded limit %d", MAX_VIEW_DEPTH)));
		}

		viewDepth++;
		ViewDefinition definition = { 0 };
		DecomposeViewDefinition(viewDefinition, &definition);

		if (definition.pipeline.value_type != BSON_TYPE_EOD)
		{
			bson_value_t *valueCopy = palloc(sizeof(bson_value_t));
			*valueCopy = definition.pipeline;
			List *stages = ExtractAggregationStages(valueCopy, context);
			if (!context->allowShardBaseTable)
			{
				/* If any nested view uses stage that can't use shard base table then
				 * we will not use shard base tables.
				 */
				canViewUseShardTable = context->allowShardBaseTable;
			}
			*pipelineStages = lappend(*pipelineStages, stages);
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
			context->allowShardBaseTable = canViewUseShardTable;
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
 * Given the pipeline definition extract the stages as a list of `AggregationStage`.
 * Performs basic validation in the structure of the pipeline.
 * If `context->optimizePipelines` is set to true, the function will optimize the pipelines.
 */
List *
ExtractAggregationStages(const bson_value_t *pipelineValue,
						 AggregationPipelineBuildContext *context)
{
	if (pipelineValue->value_type != BSON_TYPE_ARRAY ||
		IsBsonValueEmptyArray(pipelineValue))
	{
		return NIL;
	}

	bson_iter_t pipelineIterator;
	BsonValueInitIterator(pipelineValue, &pipelineIterator);

	const char *lastEncounteredOutputStage = NULL;

	List *aggregationStages = NIL;
	while (bson_iter_next(&pipelineIterator))
	{
		bson_iter_t documentIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIterator) ||
			!bson_iter_recurse(&pipelineIterator, &documentIterator))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"Each element of the 'pipeline' array must be an object")));
		}

		pgbsonelement stageElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&documentIterator, &stageElement))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40323),
							errmsg(
								"A pipeline stage specification object must contain exactly one field.")));
		}

		/* If lastEncounteredOutputStage isn't NULL, it means we've seen an output stage like $out or $merge before this.
		 * Since the output stage was expected to be last, encountering it earlier leads to failure. */
		if (lastEncounteredOutputStage != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40601),
							errmsg("%s can only be the final stage in the pipeline",
								   lastEncounteredOutputStage),
							errdetail_log(
								"%s can only be the final stage in the pipeline",
								lastEncounteredOutputStage)));
		}

		/* Get the definition of the stage */
		AggregationStageDefinition *definition = (AggregationStageDefinition *) bsearch(
			stageElement.path, StageDefinitions,
			AggregationStageCount,
			sizeof(AggregationStageDefinition),
			CompareStageByStageName);
		if (definition == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNRECOGNIZEDCOMMAND),
							errmsg("Unrecognized pipeline stage name: %s",
								   stageElement.path),
							errdetail_log("Unrecognized pipeline stage name: %s",
										  stageElement.path)));
		}
		if (definition->pipelineCheckFunc != NULL)
		{
			definition->pipelineCheckFunc(pipelineValue, context);
		}

		if (definition->mutateFunc == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Stage %s is not supported yet in native pipeline",
								definition->stage),
							errdetail_log(
								"Stage %s is not supported yet in native pipeline",
								definition->stage)));
		}

		if (definition->isOutputStage)
		{
			lastEncounteredOutputStage = definition->stage;
		}

		AggregationStage *stage = palloc0(sizeof(AggregationStage));
		stage->stageDefinition = definition;
		stage->stageValue = stageElement.bsonValue;

		aggregationStages = lappend(aggregationStages, stage);
	}

	/*
	 * Validate number of stages in the pipeline, we purposefully do it here because
	 * it saves a few CPU cycles to not check the length everytime for each stage
	 *
	 * But, because of this large pipelines will not fail as soon as they exceed the limit but eventually
	 * fail. This is a tradeoff for perf b/w most of the valid cases v/s the rare invalid ones.
	 */
	CheckMaxAllowedAggregationStages(list_length(aggregationStages));

	if (context->optimizePipelineStages)
	{
		TryOptimizeAggregationPipelines(&aggregationStages, context);
	}

	return aggregationStages;
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

	MongoCollection *collection = GetMongoCollectionOrViewByNameDatum(databaseDatum,
																	  collectionNameDatum,
																	  AccessShareLock);

	/* CollectionUUID mismatch when collection doesn't exist */
	if (collectionUuid != NULL)
	{
		if (collection == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COLLECTIONUUIDMISMATCH),
							errmsg(
								"Namespace %s has a mismatch on collectionUUID: Collection does not exist",
								context->namespaceName)));
		}

		if (memcmp(collectionUuid->data, collection->collectionUUID.data, 16) != 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COLLECTIONUUIDMISMATCH),
							errmsg("Namespace %s has a mismatch on collectionUUID",
								   context->namespaceName)));
		}
	}

	List *pipelineStages = NIL;
	if (collection != NULL && collection->viewDefinition != NULL)
	{
		collection = ExtractViewDefinitionAndPipeline(databaseDatum,
													  collection->viewDefinition,
													  &pipelineStages,
													  context);
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

		if (context->allowShardBaseTable)
		{
			Oid shardOid = TryGetCollectionShardTable(collection, AccessShareLock);
			if (shardOid != InvalidOid)
			{
				/* Mark on our copy of the collection that we're using the shard */
				collection->relationId = shardOid;
				rte->relid = shardOid;
			}
			else if (DefaultInlineWriteOperations)
			{
				context->allowShardBaseTable = true;
			}
			else
			{
				/* Signal that shard table pushdown didn't succeed */
				context->allowShardBaseTable = false;
			}
		}

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
		List *stages = list_nth(pipelineStages, i);
		query = MutateQueryWithPipeline(query, stages, context);
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
	if (queryData->cursorKind != QueryCursorType_Streamable)
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
		queryData->cursorKind = QueryCursorType_Persistent;
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
 * This function adds the continuation condition to the query for change stream,
 * to make sure the continuation token is always returned to the caller event if
 * the change document doesn't match the $match condition in the aggregation pipeline.
 * An example query after the modification here will be:
 * SELECT document, continuation
 *  FROM change_stream_aggregation(<args>)
 *  WHERE
 *   document IS NULL // This condition is added to make sure the continuation token is always returned
 *                    // even if the change document doesn't match the $match condition in the aggregation pipeline.
 *      OR
 *   <other qualifiers>
 */
static void
AddQualifierForTailableQuery(Query *query, Query *baseQuery,
							 QueryData *queryData,
							 AggregationPipelineBuildContext *context)
{
	Assert(queryData->cursorKind == QueryCursorType_Tailable);

	/* Make sure that continuation is still projected after all mutations. */
	TargetEntry *entry = llast(query->targetList);
	if (entry->resname == NULL || strcmp(entry->resname, "continuation") != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg(
							"The last target entry in the query must be the document")));
	}

	/*
	 * If there are some WHERE clause qualifiers, in a change stream query, then prepend
	 * the condition document IS NULL OR <other qualifiers> to the WHERE clause.
	 * This is needed becuase, when document IS NULL, change stream aggregation still
	 * needs to provide the continuation doc for the tailable cursor to continue the
	 * change stream query next time. So any other qualifiers should be ORed with this
	 * NULL check so that they don't filter out the document IS NULL condition.
	 *
	 *   Before transformation:
	 *      SELECT doc, continuation FROM change_stream_aggregation(<params>)
	 *           WHERE bson_dollor_match(doc) //doc.full_document.operationtype == "insert"
	 *
	 *   After transformation:
	 *      SELECT doc, continuation FROM change_stream_aggregation(<params>)
	 *           (document IS NULL) OR
	 *           (WHERE bson_dollor_match(doc) //doc.full_document.operationtype == "insert")
	 */
	if (query->jointree->quals != NULL)
	{
		Var *documentVar = CreateChangeStreamDocumentVar();
		NullTest *nullTest = makeNode(NullTest);
		nullTest->argisrow = false;
		nullTest->nulltesttype = IS_NULL;
		nullTest->arg = (Expr *) documentVar;

		/* Create the new OR clause including cursorQual */
		List *qualifierList = list_make1(query->jointree->quals);

		/* Prepend the document == NULL condition first before any other qualifiers. */
		qualifierList = lcons(nullTest, qualifierList);

		/* Add the new OR clause to the query */
		query->jointree->quals = (Node *) make_orclause(qualifierList);
	}
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28745),
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28748),
							errmsg("unrecognized option to $sample")));
		}
	}

	if (sizeValue.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28749),
						errmsg("$sample stage must specify a size")));
	}

	if (!BsonValueIsNumber(&sizeValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28746),
						errmsg("size argument to $sample must be a number")));
	}

	double sizeDouble = BsonValueAsDouble(&sizeValue);

	if (sizeDouble < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28747),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION15976),
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
CanInlineLookupStageTrue(const bson_value_t *stageValue, const StringView *lookupPath,
						 bool hasLet)
{
	return true;
}


static bool
QueryDocumentHasExpr(bson_iter_t *queryIter)
{
	check_stack_depth();
	while (bson_iter_next(queryIter))
	{
		const char *key = bson_iter_key(queryIter);
		if (strcmp(key, "$expr") == 0)
		{
			return true;
		}

		if (strcmp(key, "$and") == 0 || strcmp(key, "$or") == 0)
		{
			if (!BSON_ITER_HOLDS_ARRAY(queryIter))
			{
				continue;
			}

			bson_iter_t andOrIter;
			if (bson_iter_recurse(queryIter, &andOrIter))
			{
				while (bson_iter_next(&andOrIter))
				{
					if (!BSON_ITER_HOLDS_DOCUMENT(&andOrIter))
					{
						continue;
					}

					bson_iter_t andElementIter;
					if (bson_iter_recurse(&andOrIter, &andElementIter) &&
						QueryDocumentHasExpr(&andElementIter))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}


/*
 * Checks if $match can be inlined for $lookup
 */
static bool
CanInlineLookupStageMatch(const bson_value_t *stageValue, const
						  StringView *lookupPath, bool hasLet)
{
	if (!hasLet)
	{
		/* Without let $match can always be inlined */
		return true;
	}

	if (stageValue->value_type != BSON_TYPE_DOCUMENT)
	{
		return false;
	}

	/* With let $match can be inlined if there's no $expr */
	bson_iter_t queryIter;
	BsonValueInitIterator(stageValue, &queryIter);
	return !QueryDocumentHasExpr(&queryIter);
}


/*
 * Helper for a projection stage on whether it can be inlined for a $lookup.
 * Can be inlined if all the projection functions are not prefixes or suffixes
 * of the lookup local path.
 * We can probably do better but that is left as an exercise for later.
 */
static bool
CanInlineLookupStageProjection(const bson_value_t *stageValue, const
							   StringView *lookupPath, bool hasLet)
{
	if (stageValue->value_type != BSON_TYPE_DOCUMENT)
	{
		return false;
	}

	if (hasLet)
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
CanInlineLookupStageUnset(const bson_value_t *stageValue, const StringView *lookupPath,
						  bool hasLet)
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
CanInlineLookupStageUnwind(const bson_value_t *stageValue, const StringView *lookupPath,
						   bool hasLet)
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
				queryData->cursorKind = QueryCursorType_SingleBatch;
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unrecognized field: %s",
								   path)));
		}
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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
	check_stack_depth();
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


/*
 * We will try to optimize the aggregation stages here. Currently this method checks
 * if all the stages are referring to a single collection that is not sharded then we
 * can convert the query to push it down to the particular shard (single node)
 * TODO: Other optimizations to be followed
 * 1- Merge consecutive projection stages to be merged.
 * 2- Improve match stage if preceded by a projection stage and the filter is on a renamed field which could
 *    potentially use index.
 */
static void
TryOptimizeAggregationPipelines(List **aggregationStages,
								AggregationPipelineBuildContext *context)
{
	List *stagesList = *aggregationStages;
	if (stagesList == NIL || list_length(stagesList) == 0)
	{
		return;
	}

	/* Whether or not we can safely push the aggregation pipeline query to the shard table directly depends on
	 * if all the stages refer to a single collection and it is not sharded, only in this case it is feasible to push
	 * these queries directly to shard table.
	 */
	bool allowShardBaseTable = true;
	int nextIndex = 0;
	int currentIndex = 0;

	ListCell *cell;
	foreach(cell, stagesList)
	{
		currentIndex = foreach_current_index(cell);
		if (currentIndex < nextIndex)
		{
			continue;
		}

		AggregationStage *stage = (AggregationStage *) lfirst(cell);
		const AggregationStageDefinition *definition = stage->stageDefinition;
		if (!definition->allowBaseShardTablePushdown)
		{
			/* If any stage doesn't support it the final value is not supported */
			allowShardBaseTable = false;
		}

		if (definition->stageEnum == Stage_Lookup &&
			EnableLookupUnwindSupport && IsClusterVersionAtleast(DocDB_V0, 24, 0))
		{
			/* Optimization for $lookup stage
			 */
			if (currentIndex < list_length(stagesList) - 1)
			{
				AggregationStage *nextStage =
					(AggregationStage *) lfirst(list_nth_cell(stagesList, currentIndex +
															  1));
				if (nextStage->stageDefinition->stageEnum == Stage_Unwind)
				{
					/* If the next stage is $unwind, we can merge the $lookup and $unwind stages into a single stage.
					 * This is because $lookup followed by $unwind is a common pattern and can be optimized to a single stage,
					 * if $unwind is requested on the same field which is the "as" field in lookup stage.
					 */
					bool preserveEmptyArrays = false;
					if (CanInlineLookupWithUnwind(&stage->stageValue,
												  &nextStage->stageValue,
												  &preserveEmptyArrays))
					{
						*aggregationStages = foreach_delete_current(stagesList, cell);
						AggregationStage *lookupUnwindStage = nextStage;

						/* merge preserve empty arrays and the lookup spec */
						pgbson_writer writer;
						PgbsonWriterInit(&writer);
						PgbsonWriterAppendBool(&writer, "preserveNullAndEmptyArrays", 26,
											   preserveEmptyArrays);
						PgbsonWriterAppendValue(&writer, "lookup", 6, &stage->stageValue);

						lookupUnwindStage->stageValue = ConvertPgbsonToBsonValue(
							PgbsonWriterGetPgbson(&writer));
						lookupUnwindStage->stageDefinition =
							(AggregationStageDefinition *) &LookupUnwindStageDefinition;
					}
				}
			}
		}

		nextIndex = currentIndex + 1;
	}

	context->allowShardBaseTable = allowShardBaseTable;
}
