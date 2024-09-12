/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_expression.h
 *
 * Common declarations of the bson expressions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_EXPRESSION_H
#define BSON_EXPRESSION_H

#include <utils/hsearch.h>

#define MISSING_TYPE_NAME "missing"
#define MISSING_VALUE_NAME "MISSING"

typedef struct ExpressionResult ExpressionResult;
typedef struct BsonIntermediatePathNode BsonIntermediatePathNode;
typedef struct AggregationExpressionData AggregationExpressionData;

/*
 * A struct that defines the value for a specific variable
 */
typedef struct
{
	/* The name of the variable. */
	StringView name;

	/* Whether the variable is a constant or an expression. */
	bool isConstant;

	/* A union which holds either the constant value or the expression data. */
	union
	{
		AggregationExpressionData *expression;
		bson_value_t bsonValue;
	};
} VariableData;

/*
 * A struct that defines the variable context available for an expression.
 */
typedef struct ExpressionVariableContext
{
	/* bool to indicate if there is a single or multiple variables defined in the context. */
	bool hasSingleVariable;

	union
	{
		/* a struct representing the variable. */
		VariableData variable;

		/* a hashtable containing the variables at this context. */
		HTAB *table;
	} context;

	/* the expression's parent to be  able to traverse if the variable is not found in the current context. */
	const struct ExpressionVariableContext *parent;
} ExpressionVariableContext;

/* Func that will handle evaluating a given operator on a document. */
typedef void (*LegacyEvaluateOperator)(pgbson *doc, const bson_value_t *operatorValue,
									   ExpressionResult *writer);

/* Func that handles evaluating a preparsed operator on a given document. */
typedef void (*HandlePreParsedOperatorFunc)(pgbson *doc, void *arguments,
											ExpressionResult *expressionResult);

/* Enum that defines the kind of an aggregation expression. */
typedef enum AggregationExpressionKind
{
	/* An invalid aggregation expression kind (default value). */
	AggregationExpressionKind_Invalid = 0,

	/* An aggregation operator that is an operator, i.e: { $eq: [1, 1] } */
	AggregationExpressionKind_Operator = 1,

	/* A constant aggregation expression, i.e: a bson value representing an int, string, bool, doc, etc. */
	AggregationExpressionKind_Constant = 2,

	/* An aggregation expression referencing a path in the document being evaluated i.e: "$a". */
	AggregationExpressionKind_Path = 3,

	/* An aggregation expression referencing a variable in the given variable context i.e: $$variable. */
	AggregationExpressionKind_Variable = 4,

	/* An aggregation expression referencing a system variable in the global variable context i.e: $$variable. */
	AggregationExpressionKind_SystemVariable = 5,

	/* An aggregation expression which is an array that contains nested expressions that are not constant. */
	AggregationExpressionKind_Array = 6,

	/* An aggregation expression which is a document that contains nested expressions that are not constant. */
	AggregationExpressionKind_Document = 7,
} AggregationExpressionKind;


/* Enum that defines the kind of arguments that an expression parsed.
 * This is temporary and it is used to free memory after the operator is evaluated from the old
 * expression operators engine, since there we don't reuse the parsed data. */
typedef enum AggregationExpressionArgumentsKind
{
	/* Invalid argument kind (default value). */
	AggregationExpressionArgumentsKind_Invalid = 0,

	/* Palloc'd struct, could be any custom struct that is palloc'd (must be freed with pfree). */
	AggregationExpressionArgumentsKind_Palloc = 1,

	/* List * that must be freed with list_free_deep PG method. */
	AggregationExpressionArgumentsKind_List = 2,

	/* Empty argument kind. */
	AggregationExpressionArgumentsKind_Empty = 3
} AggregationExpressionArgumentsKind;


/* The identifiers for system variables in aggregation expressions */
typedef enum AggregationExpressionSystemVariableKind
{
	/* The $$NOW variable */
	AggregationExpressionSystemVariableKind_Now = 1,

	/* The $$CLUSTER_TIME variable */
	AggregationExpressionSystemVariableKind_ClusterTime = 2,

	/* The $$ROOT variable */
	AggregationExpressionSystemVariableKind_Root = 3,

	/* The $$CURRENT variable */
	AggregationExpressionSystemVariableKind_Current = 4,

	/* The $$REMOVE variable */
	AggregationExpressionSystemVariableKind_Remove = 5,

	/* The $$DESCEND variable */
	AggregationExpressionSystemVariableKind_Descend = 6,

	/* The $$PRUNE variable */
	AggregationExpressionSystemVariableKind_Prune = 7,

	/* The $$KEEP variable */
	AggregationExpressionSystemVariableKind_Keep = 8,

	/* The $$SEARCH_META variable */
	AggregationExpressionSystemVariableKind_SearchMeta = 9,

	/* The $$USER_ROLES variable */
	AggregationExpressionSystemVariableKind_UserRoles = 10,
} AggregationExpressionSystemVariableKind;


/* Struct representing an aggregation expression containing the necessary data in order to evaluate it. */
typedef struct AggregationExpressionData
{
	/* The kind of the aggregation expression to know how to evaluate the expression. */
	AggregationExpressionKind kind;

	union
	{
		/* The value representing a constant expression (AggregationExpressionKind_Constant). */
		bson_value_t value;

		/* The root node for a tree representing a document or array expression (which are not constant values). */
		const BsonIntermediatePathNode *expressionTree;

		/* A struct containing the data for an operator expression in order to evaluate the operator. */
		struct
		{
			/* The kind of pointer typed used for the arguments, this is in order to free the allocated memory when an operator is parsed in the OLD framework.
			 * Will remove once all expressions implement the pre-evaluated framework. */
			AggregationExpressionArgumentsKind argumentsKind;

			/* The arguments for the operator. */
			void *arguments;

			/* The function that evaluates the pre-parsed operator. */
			HandlePreParsedOperatorFunc handleExpressionFunc;

			/* Legacy function to evaluate an operator. Will remove once all expressions implement the pre-evaluated framework. */
			LegacyEvaluateOperator legacyEvaluateOperatorFunc;

			/* The return type for the operator which will help do validations or optimizations when parsing the expressions. */
			bson_type_t returnType;

			/* The document for the operator, used to evaluate an operator that is not pre parsed. Will remove once all expressions implement the pre-evaluated framework. */
			bson_value_t expressionValue;
		} operator;

		struct
		{
			/* The enum tracking the system variable (if kind is AggregationExpressionKind_SystemVariable ) */
			AggregationExpressionSystemVariableKind kind;

			/* A path suffix if the variable has a sub dotted path (e.g. for $$ROOT.a.b will be a.b) */
			StringView pathSuffix;
		} systemVariable;
	};
} AggregationExpressionData;


/* Func that is called after every aggregation expression is parsed to check if it is valid on the current context, i.e let in top level commands like find can't have path expressions ($a) nor use $$CURRENT/$$ROOT system variables. */
typedef void (*ValidateParsedAggregationExpression)(AggregationExpressionData *data);

/* Struct to pass down at parse time of the aggregation expressions that sets the information of what kind of expressions were found on the expression tree.*/
typedef struct ParseAggregationExpressionContext
{
	/* Function that is called after every aggregation expression is parsed. */
	ValidateParsedAggregationExpression validateParsedExpressionFunc;
} ParseAggregationExpressionContext;


void EvaluateExpressionToWriter(pgbson *document, const pgbsonelement *element,
								pgbson_writer *writer,
								ExpressionVariableContext *variableContext,
								bool isNullOnEmpty);
void EvaluateAggregationExpressionDataToWriter(const
											   AggregationExpressionData *expressionData,
											   pgbson *document, StringView path,
											   pgbson_writer *writer,
											   const ExpressionVariableContext *
											   variableContext,
											   bool isNullOnEmpty);
void ParseAggregationExpressionData(AggregationExpressionData *expressionData,
									const bson_value_t *value,
									ParseAggregationExpressionContext *context);
void ParseVariableSpec(const bson_value_t *variableSpec,
					   ExpressionVariableContext *variableContext,
					   ParseAggregationExpressionContext *parseContext);
void VariableContextSetVariableData(ExpressionVariableContext *variableContext, const
									VariableData *variableData);
void ValidateVariableName(StringView name);

#endif
