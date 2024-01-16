/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_update_operators.h
 *
 * Private and update operator declarations of functions for handling bson updates.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_UPDATE_OPERATORS_H
#define BSON_UPDATE_OPERATORS_H

#include "io/helio_bson_core.h"
#include "operators/bson_expr_eval.h"
#include "utils/mongo_errors.h"


/*
 * State that is passed to the update operators when writing the
 * update values. Contains global state that is pertinent to the
 * current document being updated.
 */
typedef struct CurrentDocumentState
{
	/*
	 * The document ID of the document being updated
	 */
	const bson_value_t documentId;

	/* Whether or not updating the current document is an upsert
	 * operation
	 */
	bool isUpsert;

	/* The source document used to evaluate specific update scenarios
	 * like rename etc.
	 */
	pgbson *sourceDocument;
} CurrentDocumentState;


/*
 * State that is passed to each update function
 * that is pertinent to the field path that is being
 * updated. Contains information that is used for error logging
 * and diagnostics.
 */
typedef struct UpdateSetValueState
{
	/* The update field path (contains only the current field) */
	const StringView *fieldPath;

	/* The relative dotted path from the root to the field */
	const char *relativePath;

	/* Whether the update path is in an array context */
	bool isArray;

	/* Whether or not the ancestors of this node has arrays */
	bool hasArrayAncestors;
} UpdateSetValueState;

/*
 * Common State for $pull update operator based on the updateSpec
 */
typedef struct BsonUpdateDollarPullState
{
	/* Compiled expression for $pull spec of a field */
	ExprEvalState *evalState;

	/*
	 * This would be true if $pull spec is a non-doc value or doc value without Expression
	 * e.g.: {a: 2} or {a: {b: 2}}
	 */
	bool isValue;
} BsonUpdateDollarPullState;


/* Declare a writer type for writing responses
 * This abstracts tracking the modified value and building the changestream spec.
 */
typedef struct UpdateOperatorWriter UpdateOperatorWriter;
typedef struct UpdateArrayWriter UpdateArrayWriter;

/* These work similar to element writer functions except they track whether
 * there was any modifications */
void UpdateWriterWriteModifiedValue(UpdateOperatorWriter *writer,
									const bson_value_t *value);
void UpdateWriterSkipValue(UpdateOperatorWriter *writer);

/* UpdateWriter functions for update operators that modify arrays. here the operator
 * is responsible for writing original values, and modified values into the array
 * and finalizing it similar to the array-writer interface.
 */
UpdateArrayWriter * UpdateWriterGetArrayWriter(UpdateOperatorWriter *writer);
void UpdateArrayWriterWriteOriginalValue(UpdateArrayWriter *writer,
										 const bson_value_t *value);
void UpdateArrayWriterWriteModifiedValue(UpdateArrayWriter *writer,
										 const bson_value_t *value);
void UpdateArrayWriterSkipValue(UpdateArrayWriter *writer);
void UpdateArrayWriterFinalize(UpdateOperatorWriter *writer,
							   UpdateArrayWriter *arrayWriter);


/* operators */
void HandleUpdateDollarSet(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarInc(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarMin(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarMax(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarUnset(const bson_value_t *existingValue,
							 UpdateOperatorWriter *writer,
							 const bson_value_t *updateValue,
							 void *updateNodeContext,
							 const UpdateSetValueState *setValueState,
							 const CurrentDocumentState *state);
void HandleUpdateDollarMul(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarBit(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarCurrentDate(const bson_value_t *existingValue,
								   UpdateOperatorWriter *writer,
								   const bson_value_t *updateValue,
								   void *updateNodeContext,
								   const UpdateSetValueState *setValueState,
								   const CurrentDocumentState *state);
void HandleUpdateDollarRenameSource(const bson_value_t *existingValue,
									UpdateOperatorWriter *writer,
									const bson_value_t *updateValue,
									void *updateNodeContext,
									const UpdateSetValueState *setValueState,
									const CurrentDocumentState *state);
void HandleUpdateDollarRename(const bson_value_t *existingValue,
							  UpdateOperatorWriter *writer,
							  const bson_value_t *updateValue,
							  void *updateNodeContext,
							  const UpdateSetValueState *setValueState,
							  const CurrentDocumentState *state);
void HandleUpdateDollarSetOnInsert(const bson_value_t *existingValue,
								   UpdateOperatorWriter *writer,
								   const bson_value_t *updateValue,
								   void *updateNodeContext,
								   const UpdateSetValueState *setValueState,
								   const CurrentDocumentState *state);
void HandleUpdateDollarAddToSet(const bson_value_t *existingValue,
								UpdateOperatorWriter *writer,
								const bson_value_t *updateValue,
								void *updateNodeContext,
								const UpdateSetValueState *setValueState,
								const CurrentDocumentState *state);
void HandleUpdateDollarPullAll(const bson_value_t *existingValue,
							   UpdateOperatorWriter *writer,
							   const bson_value_t *updateValue,
							   void *updateNodeContext,
							   const UpdateSetValueState *setValueState,
							   const CurrentDocumentState *state);
void HandleUpdateDollarPush(const bson_value_t *existingValue,
							UpdateOperatorWriter *writer,
							const bson_value_t *updateValue,
							void *updateNodeContext,
							const UpdateSetValueState *setValueState,
							const CurrentDocumentState *state);
void HandleUpdateDollarPop(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state);
void HandleUpdateDollarPull(const bson_value_t *existingValue,
							UpdateOperatorWriter *writer,
							const bson_value_t *updateValue,
							void *updateNodeContext,
							const UpdateSetValueState *setValueState,
							const CurrentDocumentState *state);

#endif
