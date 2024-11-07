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

ExprEvalState * PrepareForSchemaValidation(pgbson *schemaValidationInfo, MemoryContext
										   memoryContext);

void ValidateSchemaOnDocumentInsert(ExprEvalState *evalState, const
									bson_value_t *document);

void ValidateSchemaOnDocumentUpdate(ValidationLevels validationLevelText,
									ExprEvalState *evalState,
									pgbson *sourceDocument, pgbson *targetDocument);

/* Inline function that determines whether to perform schema validation */
inline bool
CheckSchemaValidationEnabled(MongoCollection *collection, bool bypassDocumentValidation)
{
	return EnableSchemaValidation && collection->schemaValidator.validationLevel !=
		   ValidationLevel_Off && !bypassDocumentValidation &&
		   collection->schemaValidator.validator != NULL &&
		   collection->schemaValidator.validationAction == ValidationAction_Error;
}


#endif
