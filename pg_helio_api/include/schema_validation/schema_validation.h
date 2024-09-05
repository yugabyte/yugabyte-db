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

ExprEvalState * PrepareForSchemaValidation(pgbson *schemaValidationInfo, MemoryContext
										   memoryContext);

void ValidateSchemaOnDocumentInsert(ExprEvalState *evalState, const
									bson_value_t *document);

#endif
