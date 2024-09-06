/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_update_aggregation.c
 *
 * Implementation of the update operation for aggregation update operators.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "aggregation/bson_project.h"
#include "utils/mongo_errors.h"
#include "update/bson_update_common.h"
#include "commands/commands_common.h"


struct UpdateAggregationSpec;

/*
 * HandleUpdateAggregationOperator takes a source document and an aggregation specification
 * and applies the aggregation spec to the source document.
 */
typedef void (*HandleUpdateAggregationOperator)(pgbson **sourceDoc,
												const struct
												UpdateAggregationSpec *aggregationSpec);


/*
 * PopulateAggregationStateOperatorFunc takes an aggregation operator specification value and
 * populates the UpdateAggregationState with the compiled/parsed aggregation spec
 * that can be used to apply the aggregation stages across multiple documents.
 */
typedef void (*PopulateAggregationStateOperatorFunc)(const bson_value_t *operator, struct
													 UpdateAggregationSpec *state);


/*
 * Declaration of an aggregation operator
 */
typedef struct
{
	/* The name of hte update operator e.g. $set */
	const char *operatorName;

	/* Function that handles updating a document with
	 * aggregation pipeline operators
	 */
	HandleUpdateAggregationOperator updateFunc;

	/*
	 * Function that handles getting update projection tree
	 * state given an aggregation pipeline bson value.
	 */
	PopulateAggregationStateOperatorFunc populateFunc;
} MongoUpdateAggregationOperator;


/*
 * The parsed/processed specification of an update aggregation
 * stage operator derived from an update spec.
 * This will be used to apply the update on input documents.
 * This is set by each operator during the initial stage
 * of building/handling the update.
 */
typedef struct UpdateAggregationSpec
{
	union
	{
		/* The projection query state (opaque structure)
		 * that is passed back to the projection execution
		 * to produce the final output document.
		 */
		const struct BsonProjectionQueryState *queryState;

		/*
		 * The expression data necessary to project a document (primarily used)
		 * in the scenario of replaceRoot.
		 */
		AggregationExpressionData expressionData;
	};

	/* Whether or not the state is a expression */
	bool isExpression;
} UpdateAggregationSpec;


/*
 * Data that tracks the information necessary to process
 * documents - this includes a pointer to the function that
 * will apply updates to documents based on an aggregation
 * stage spec, and the associated spec that the operator provided
 * for the Update's input specification.
 */
typedef struct UpdateAggregationStageData
{
	HandleUpdateAggregationOperator updateFunc;
	UpdateAggregationSpec state;
} UpdateAggregationStageData;


/*
 * Top level metadata that tracks the list of
 * parsed and evaluated aggregation pipeline stages that will be
 * run against source documents to produce the updated doc.
 */
typedef struct AggregationPipelineUpdateState
{
	List *aggregationStages;
} AggregationPipelineUpdateState;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

/* Aggregation stage handlers */

static void PopulateDollarProjectState(const bson_value_t *updateSpec,
									   UpdateAggregationSpec *aggregationSpec);
static void PopulateDollarUnsetState(const bson_value_t *updateSpec,
									 UpdateAggregationSpec *aggregationSpec);
static void PopulateDollarAddFieldsState(const bson_value_t *updateSpec,
										 UpdateAggregationSpec *aggregationSpec);
static void PopulateDollarReplaceRootState(const bson_value_t *updateSpec,
										   UpdateAggregationSpec *aggregationSpec);
static void PopulateDollarReplaceWithState(const bson_value_t *updateSpec,
										   UpdateAggregationSpec *aggregationSpec);

static void HandleUpdateProjectionState(pgbson **source, const
										UpdateAggregationSpec *updateSpec);
static void HandleUpdateReplaceRoot(pgbson **source, const
									UpdateAggregationSpec *updateSpec);
static void ValidateReplaceRootElement(const bson_value_t *value);

static MongoUpdateAggregationOperator AggregationOperators[] =
{
	{ "$project", &HandleUpdateProjectionState, &PopulateDollarProjectState },
	{ "$unset", &HandleUpdateProjectionState, &PopulateDollarUnsetState },
	{ "$addFields", &HandleUpdateProjectionState, &PopulateDollarAddFieldsState },
	{ "$set", &HandleUpdateProjectionState, &PopulateDollarAddFieldsState },    /* alias of addFields */
	{ "$replaceRoot", &HandleUpdateReplaceRoot, &PopulateDollarReplaceRootState },
	{ "$replaceWith", &HandleUpdateReplaceRoot, &PopulateDollarReplaceWithState },

	{ NULL, NULL, NULL },
};


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/*
 * Given a document specified by the updateSpec, walks the updateSpec's
 * aggregation pipeline specification and builds and processes the state
 * into the AggregationPipelineUpdateState that can then be used to evaluate
 * the pipeline against documents to produce modified documents.
 */
struct AggregationPipelineUpdateState *
GetAggregationPipelineUpdateState(pgbson *updateSpec)
{
	bson_iter_t updateIterator;
	PgbsonInitIteratorAtPath(updateSpec, "", &updateIterator);
	if (!BSON_ITER_HOLDS_ARRAY(&updateIterator) ||
		!bson_iter_recurse(&updateIterator, &updateIterator))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"aggregation pipeline should be an array")));
	}

	List *aggregationStages = NIL;
	while (bson_iter_next(&updateIterator))
	{
		bson_iter_t aggregationIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&updateIterator) ||
			!bson_iter_recurse(&updateIterator, &aggregationIterator))
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
								"aggregation should be a document")));
		}

		/* process each aggregation expression individually */
		pgbsonelement aggregationElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&aggregationIterator,
													   &aggregationElement))
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
								"aggregation document should have a single operator")));
		}

		int i = 0;
		bool operatorFound = false;
		while (AggregationOperators[i].operatorName != NULL)
		{
			if (strcmp(aggregationElement.path,
					   AggregationOperators[i].operatorName) != 0)
			{
				i++;
				continue;
			}

			if (AggregationOperators[i].populateFunc == NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED), errmsg(
									"%s not supported yet",
									aggregationElement.path), errdetail_log(
									"%s not supported yet",
									aggregationElement.path)));
			}

			operatorFound = true;

			UpdateAggregationStageData *stageData = palloc0(
				sizeof(UpdateAggregationStageData));
			stageData->updateFunc = AggregationOperators[i].updateFunc;
			AggregationOperators[i].populateFunc(&aggregationElement.bsonValue,
												 &stageData->state);
			aggregationStages = lappend(aggregationStages, stageData);

			break;
		}

		if (!operatorFound)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDOPTIONS), errmsg(
								"Invalid aggregation pipeline operator for update %s",
								aggregationElement.path)));
		}
	}

	AggregationPipelineUpdateState *cachedState = palloc0(
		sizeof(AggregationPipelineUpdateState));
	cachedState->aggregationStages = aggregationStages;
	return cachedState;
}


/*
 * ProcessAggregationPipelineUpdate takes a source document, and an update
 * specification that is an aggregation pipeline array that has the stages
 * $set, $addFields, $unset, $project, $replaceRoot or $replaceWith (only stages supported
 * by the mongo protocol) and applies the pipeline on the update document.
 * In this case, we apply each stage as a separate entity and rewrite the document in-between
 * stages. This is because mutations from a prior stage are visible as inputs to the next stage.
 * This may be scenarios such as - Set an array in one stage, and then set the array index
 * in a subsequent stage. This would require materialization of the array which only happens when we
 * write it out.
 *
 * TODO: Need to decide whether the update was a no-op by taking all the stages
 *       of the aggregation-pipelined update. To do that, we might want to use a
 *       single update-spec tree across all the stages.
 *
 *       So that we can return NULL here for no-op updates and then we can drop
 *       PgbsonEquals() check from the callsite.
 */
pgbson *
ProcessAggregationPipelineUpdate(pgbson *sourceDoc,
								 const AggregationPipelineUpdateState *
								 updateState,
								 bool isUpsert)
{
	bson_iter_t sourceDocIterator;
	bson_iter_t finalDocIterator;

	const bson_value_t *previousIdValue = NULL;
	if (!isUpsert)
	{
		/* extract the _id. This is used later on in validation of _id */
		if (!PgbsonInitIteratorAtPath(sourceDoc, "_id", &sourceDocIterator))
		{
			ereport(ERROR, (errmsg(
								"Internal error - source document did not have an id for a non upsert case")));
		}

		previousIdValue = bson_iter_value(&sourceDocIterator);
	}

	/* process each aggregation pipeline stage */
	pgbson *finalDocument = sourceDoc;

	ListCell *stageCell;
	foreach(stageCell, updateState->aggregationStages)
	{
		UpdateAggregationStageData *stageData = lfirst(stageCell);

		stageData->updateFunc(&finalDocument, &stageData->state);
	}

	if (isUpsert)
	{
		/* ensure _id is there and is at the top of the document */
		finalDocument = RewriteDocumentAddObjectId(finalDocument);

		/* one last validation on the final document. */
		PgbsonValidateInputBson(finalDocument, BSON_VALIDATE_NONE);
		return finalDocument;
	}

	/* ensure that the document _id is not modified */
	if (previousIdValue == NULL)
	{
		ereport(ERROR, (errmsg(
							"Internal error - Unexpected - did not extract _id from source document")));
	}

	if (!PgbsonInitIteratorAtPath(finalDocument, "_id", &finalDocIterator))
	{
		ThrowIdPathModifiedErrorForOperatorUpdate();
	}

	const bson_value_t *newIdValue = bson_iter_value(&finalDocIterator);
	if (!BsonValueEquals(previousIdValue, newIdValue))
	{
		ThrowIdPathModifiedErrorForOperatorUpdate();
	}

	/* Validate that the document ID is correct */
	ValidateIdField(newIdValue);

	/* one last validation on the final document. */
	PgbsonValidateInputBson(finalDocument, BSON_VALIDATE_NONE);
	return finalDocument;
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */


/*
 * Wrapper around projection functions that call into the Projection code with the
 * pre-constructed BsonProjectQueryState.
 */
static void
HandleUpdateProjectionState(pgbson **source, const UpdateAggregationSpec *projectionValue)
{
	Assert(!projectionValue->isExpression);
	pgbson *finalDoc = ProjectDocumentWithState(*source, projectionValue->queryState);
	*source = finalDoc;
}


/*
 * Wrapper around $replaceRoot for update that calls the replaceRoot function.
 */
static void
HandleUpdateReplaceRoot(pgbson **source, const UpdateAggregationSpec *projectionValue)
{
	Assert(projectionValue->isExpression);

	/*
	 * forcing replaceRoot to project _id in the update pipeline. Without _id,
	 * update pipeline fails as _id is immutable
	 */
	bool forceProjectId = true;
	pgbson *sourceDoc = *source;
	const ExpressionVariableContext *variableContext = NULL;
	*source = ProjectReplaceRootDocument(sourceDoc, &projectionValue->expressionData,
										 variableContext,
										 forceProjectId);
}


/*
 * Validates the projection value, and then constructs a
 * BsonProjectionQueryState that is valid for $project.
 */
static void
PopulateDollarProjectState(const bson_value_t *projectionValue,
						   UpdateAggregationSpec *aggregationSpec)
{
	if (projectionValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"$project should be a document")));
	}

	bson_iter_t projectionSpec;
	bson_iter_init_from_data(&projectionSpec,
							 projectionValue->value.v_doc.data,
							 projectionValue->value.v_doc.data_len);

	bool forceProjectId = true;
	bool allowInclusionExclusion = false;
	aggregationSpec->queryState = GetProjectionStateForBsonProject(&projectionSpec,
																   forceProjectId,
																   allowInclusionExclusion);
	aggregationSpec->isExpression = false;
}


/*
 * Validates the unset value, and then constructs a
 * BsonProjectionQueryState that is valid for $unset.
 */
static void
PopulateDollarUnsetState(const bson_value_t *unsetValue,
						 UpdateAggregationSpec *aggregationSpec)
{
	bool forceProjectId = true;
	aggregationSpec->queryState = GetProjectionStateForBsonUnset(unsetValue,
																 forceProjectId);
	aggregationSpec->isExpression = false;
}


/*
 * Validates the addFields value, and then constructs a
 * BsonProjectionQueryState that is valid for $addFields.
 */
static void
PopulateDollarAddFieldsState(const bson_value_t *addFieldsValue,
							 UpdateAggregationSpec *aggregationSpec)
{
	if (addFieldsValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"$addFields should be a document")));
	}

	bson_iter_t addFieldsSpec;
	bson_iter_init_from_data(&addFieldsSpec,
							 addFieldsValue->value.v_doc.data,
							 addFieldsValue->value.v_doc.data_len);

	aggregationSpec->queryState = GetProjectionStateForBsonAddFields(&addFieldsSpec);
	aggregationSpec->isExpression = false;
}


/*
 * Validates the replaceRoot value, and then constructs a
 * pgbsonelement that is valid for $replaceRoot.
 */
static void
PopulateDollarReplaceRootState(const bson_value_t *replaceRootValue,
							   UpdateAggregationSpec *aggregationSpec)
{
	if (replaceRootValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"$replaceRoot should be a document")));
	}

	bson_iter_t replaceRootSpec;
	bson_iter_init_from_data(&replaceRootSpec,
							 replaceRootValue->value.v_doc.data,
							 replaceRootValue->value.v_doc.data_len);

	bson_value_t bsonValue;
	GetBsonValueForReplaceRoot(&replaceRootSpec, &bsonValue);
	ValidateReplaceRootElement(&bsonValue);

	/* TODO VARIABLE flow variable from update spec. */
	ParseAggregationExpressionContext parseContext = { 0 };
	ParseAggregationExpressionData(&aggregationSpec->expressionData, &bsonValue,
								   &parseContext);
	aggregationSpec->isExpression = true;
}


/*
 * Sets the updateState for a $replaceWith operation.
 */
static void
PopulateDollarReplaceWithState(const bson_value_t *replaceRootValue,
							   UpdateAggregationSpec *aggregationSpec)
{
	ValidateReplaceRootElement(replaceRootValue);

	/* TODO VARIABLE flow variable from update spec. */
	ParseAggregationExpressionContext parseContext = { 0 };
	ParseAggregationExpressionData(&aggregationSpec->expressionData, replaceRootValue,
								   &parseContext);
	aggregationSpec->isExpression = true;
}


static void
ValidateReplaceRootElement(const bson_value_t *value)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();
	if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t docIter;
		BsonValueInitIterator(value, &docIter);
		while (bson_iter_next(&docIter))
		{
			StringView keyView = bson_iter_key_string_view(&docIter);
			if (keyView.length > 0 && keyView.string[0] == '$')
			{
				/* Treat as expression (let expression evaluation handle the error) */
				continue;
			}

			if (StringViewContains(&keyView, '.'))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("FieldPath field names may not contain '.'."
									   " Consider using $getField or $setField")));
			}

			ValidateReplaceRootElement(bson_iter_value(&docIter));
		}
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);
		while (bson_iter_next(&arrayIter))
		{
			ValidateReplaceRootElement(bson_iter_value(&arrayIter));
		}
	}
}
