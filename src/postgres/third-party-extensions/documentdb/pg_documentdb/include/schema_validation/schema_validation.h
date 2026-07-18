/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/schema_validation.h
 *
 * Common declarations for schema validation functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef SCHEMA_VALIDATION_H
#define SCHEMA_VALIDATION_H

#include "metadata/collection.h"
#include "operators/bson_expr_eval.h"
extern bool EnableSchemaValidation;

#define FAILED_VALIDATION_ERROR_MSG "Document failed validation"
#define FAILED_VALIDATION_PLAN_EXECUTOR_ERROR_MSG \
	"PlanExecutor error during aggregation :: caused by :: Document failed validation"

ExprEvalState * PrepareForSchemaValidation(pgbson *schemaValidationInfo, MemoryContext
										   memoryContext);
void AssignSchemaValidationState(ExprEvalState *state, pgbson *schemaValidationInfo,
								 MemoryContext memoryContext);
void ValidateSchemaOnDocumentInsert(ExprEvalState *evalState, const
									bson_value_t *document, const char *errMsg);

void ValidateSchemaOnDocumentUpdate(ValidationLevels validationLevelText,
									ExprEvalState *evalState,
									const pgbson *sourceDocument,
									const pgbson *targetDocument,
									const char *errMsg);

/* Inline function that determines whether to perform schema validation */
static inline bool
CheckSchemaValidationEnabled(MongoCollection *collection, bool bypassDocumentValidation)
{
	return EnableSchemaValidation && collection->schemaValidator.validationLevel !=
		   ValidationLevel_Off && !bypassDocumentValidation &&
		   collection->schemaValidator.validator != NULL &&
		   collection->schemaValidator.validationAction == ValidationAction_Error;
}


#endif
