/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/schema_validation.c
 *
 * Implementation of schema validation.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include "utils/feature_counter.h"
#include "operators/bson_expr_eval.h"
#include "schema_validation/schema_validation.h"
#include "metadata/collection.h"
#include "utils/mongo_errors.h"

extern bool EnableSchemaValidation;

/*
 * Prepare for schema validation.
 * If schema validation is not enabled, or the collection does not have a schema
 * validator, return NULL.
 * Otherwise, return the expression evaluation state for the schema validator.
 */
ExprEvalState *
PrepareForSchemaValidation(pgbson *schemaValidationInfo, MemoryContext memoryContext)
{
	bson_value_t validatorBsonValue = ConvertPgbsonToBsonValue(
		schemaValidationInfo);
	if (validatorBsonValue.value_type == BSON_TYPE_NULL ||
		validatorBsonValue.value_type == BSON_TYPE_EOD)
	{
		return NULL;
	}

	/*todo: cannot support top level query operator like $jsonSchema/$expr for now */
	return GetExpressionEvalState(
		&validatorBsonValue,
		memoryContext);
}


/*
 * Validate a document against the schema validator.
 * Only if validation action is set to error, we validate document and throw an error if the document does not match the schema validator.
 * If the validation action is set to warn, we do nothing and would not call this function.
 */
void
ValidateSchemaOnDocumentInsert(ExprEvalState *evalState, const
							   bson_value_t *document)
{
	bool shouldRecurseIfArray = false;
	bool matched = EvalBooleanExpressionAgainstValue(evalState, document,
													 shouldRecurseIfArray);
	if (!matched)
	{
		/* native mongo return additional information about the cause of the failure */
		ereport(ERROR, (errcode(MongoDocumentFailedValidation),
						errmsg("Document failed validation")));
	}
}
