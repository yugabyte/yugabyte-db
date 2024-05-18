/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression.c
 *
 * Operator expression implementations of BSON.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>
#include <executor/executor.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <parser/parse_coerce.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_write.h"
#include "aggregation/bson_projection_tree.h"
#include "utils/feature_counter.h"
#include "utils/hashset_utils.h"
#include "utils/fmgr_utils.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

typedef void (*ParseAggregationExpressionFunc)(const bson_value_t *argument,
											   AggregationExpressionData *data,
											   const ExpressionVariableContext *
											   variableContext);

/*
 * The declaration of an operator used in OperatorExpressions[] below.
 * Every supported operator should register this datastructure there.
 */
typedef struct
{
	/* The mongodb name of the operator (e.g. $literal) */
	const char *operatorName;

	/* A function pointer that will evaluate the operator against a document. */
	LegacyEvaluateOperator legacyHandleOperatorFunc;

	/* Function pointer to parse the aggregation expression. */
	ParseAggregationExpressionFunc parseAggregationExpressionFunc;

	/* Function pointer to evaluate a pre parsed expression. */
	HandlePreParsedOperatorFunc handlePreParsedOperatorFunc;

	/* The feature counter type in order to report feature usage telemetry. */
	FeatureType featureCounterId;
} MongoOperatorExpression;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void GetAndEvaluateOperator(pgbson *document, const char *operatorName,
								   const bson_value_t *operatorValue,
								   ExpressionResult *expressionResult);
static bool EvaluateFieldPathAndWriteCore(bson_iter_t *document,
										  const char *dottedPathExpression,
										  uint32_t dottedPathExpressionLength,
										  pgbson_element_writer *writer, bool
										  isNullOnEmpty);
static bool EvaluateFieldPathAndWrite(bson_value_t *value,
									  const char *dottedPathExpression,
									  uint32_t dottedPathExpressionLength,
									  pgbson_element_writer *writer,
									  bool isNullOnEmpty);
static void EvaluateExpressionArrayToWriter(pgbson *document,
											bson_iter_t *elementIterator,
											pgbson_element_writer *writer,
											ExpressionLifetimeTracker *tracker,
											ExpressionVariableContext *variableContext,
											bool isNullOnEmpty);
static void EvaluateExpressionObjectToWriter(pgbson *document, const
											 bson_value_t *expressionValue,
											 pgbson_element_writer *writer,
											 ExpressionLifetimeTracker *tracker,
											 ExpressionVariableContext *variableContext,
											 bool isNullOnEmpty);
static void EvaluateAggregationExpressionDocumentToWriter(const
														  AggregationExpressionData *data,
														  pgbson *document,
														  pgbson_element_writer *writer,
														  const ExpressionVariableContext
														  *
														  variableContext,
														  bool isNullOnEmpty);
static void EvaluateAggregationExpressionArrayToWriter(const
													   AggregationExpressionData *data,
													   pgbson *document,
													   pgbson_array_writer *arrayWriter,
													   const ExpressionVariableContext *
													   variableContext);
static void EvaluateAggregationExpressionVariable(const
												  AggregationExpressionData *data,
												  pgbson *document,
												  ExpressionResult *expressionResult,
												  bool isNullOnEmpty);
static void EvaluateAggregationExpressionSystemVariable(const
														AggregationExpressionData
														*data,
														pgbson *document,
														ExpressionResult *expressionResult,
														bool isNullOnEmpty);
static void ParseDocumentAggregationExpressionData(const bson_value_t *value,
												   AggregationExpressionData *
												   expressionData,
												   const ExpressionVariableContext *
												   variableContext);
static void ParseArrayAggregationExpressionData(const bson_value_t *value,
												AggregationExpressionData *expressionData,
												const ExpressionVariableContext *
												variableContext);
static bool GetVariableValueFromData(const VariableData *variable,
									 pgbson *currentDocument,
									 ExpressionResult *parentExpressionResult,
									 bson_value_t *variableValue);
static bool ExpressionResultGetVariable(StringView variableName,
										ExpressionResult *expressionResult,
										pgbson *currentDocument,
										bson_value_t *variableValue);
static void InsertVariableToContextTable(const VariableData *variableElement,
										 HTAB *hashTable);
static bool IsOverridableSystemVariable(StringView *name);
static void VariableContextSetVariableData(ExpressionVariableContext *variableContext,
										   const VariableData *variableData);

static void ReportOperatorExpressonSyntaxError(const char *fieldA,
											   bson_iter_t *fieldBIter, bool
											   performOperatorCheck);
static int CompareOperatorExpressionByName(const void *a, const void *b);
static uint32 VariableHashEntryHashFunc(const void *obj, size_t objsize);
static int VariableHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize);

/*
 *  Keep this list lexicographically sorted by the operator name,
 *  as it is binary searched on the key to find the handler.
 */
static MongoOperatorExpression OperatorExpressions[] = {
	{ "$abs", &HandleDollarAbs, NULL, NULL, FEATURE_AGG_OPERATOR_ABS },
	{ "$accumulator", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ACCUMULATOR },
	{ "$acos", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ACOS },
	{ "$acosh", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ACOSH },
	{ "$add", &HandleDollarAdd, NULL, NULL, FEATURE_AGG_OPERATOR_ADD },
	{ "$addToSet", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ADDTOSET },
	{ "$allElementsTrue", &HandleDollarAllElementsTrue, NULL, NULL,
	  FEATURE_AGG_OPERATOR_ALLELEMENTSTRUE },
	{ "$and", &HandleDollarAnd, NULL, NULL, FEATURE_AGG_OPERATOR_AND },
	{ "$anyElementTrue", &HandleDollarAnyElementTrue, NULL, NULL,
	  FEATURE_AGG_OPERATOR_ANYELEMENTTRUE },
	{ "$arrayElemAt", &HandleDollarArrayElemAt, NULL, NULL,
	  FEATURE_AGG_OPERATOR_ARRAYELEMAT },
	{ "$arrayToObject", &HandleDollarArrayToObject, NULL, NULL,
	  FEATURE_AGG_OPERATOR_ARRAYTOOBJECT },
	{ "$asin", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ASIN },
	{ "$asinh", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ASINH },
	{ "$atan", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ATAN },
	{ "$atan2", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ATAN2 },
	{ "$atanh", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_ATANH },
	{ "$avg", NULL, &ParseDollarAvg, &HandlePreParsedDollarAvg,
	  FEATURE_AGG_OPERATOR_AVG },
	{ "$binarySize", NULL, &ParseDollarBinarySize, &HandlePreParsedDollarBinarySize,
	  FEATURE_AGG_OPERATOR_BINARYSIZE },
	{ "$bsonSize", NULL, &ParseDollarBsonSize, &HandlePreParsedDollarBsonSize,
	  FEATURE_AGG_OPERATOR_BSONSIZE },
	{ "$ceil", &HandleDollarCeil, NULL, NULL, FEATURE_AGG_OPERATOR_CEIL },
	{ "$cmp", NULL, &ParseDollarCmp, &HandlePreParsedDollarCmp,
	  FEATURE_AGG_OPERATOR_CMP },
	{ "$concat", &HandleDollarConcat, NULL, NULL, FEATURE_AGG_OPERATOR_CONCAT },
	{ "$concatArrays", &HandleDollarConcatArrays, NULL, NULL,
	  FEATURE_AGG_OPERATOR_CONCATARRAYS },
	{ "$cond", &HandleDollarCond, NULL, NULL, FEATURE_AGG_OPERATOR_COND },
	{ "$const", NULL, &ParseDollarLiteral, NULL, FEATURE_AGG_OPERATOR_CONST }, /* $const effectively same as $literal */
	{ "$convert", &HandleDollarConvert, NULL, NULL, FEATURE_AGG_OPERATOR_CONVERT },
	{ "$cos", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_COS },
	{ "$cosh", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_COSH },
	{ "$dateAdd", NULL, &ParseDollarDateAdd, &HandlePreParsedDollarDateAdd,
	  FEATURE_AGG_OPERATOR_DATEADD },
	{ "$dateDiff", NULL, &ParseDollarDateDiff, &HandlePreParsedDollarDateDiff,
	  FEATURE_AGG_OPERATOR_DATEDIFF },
	{ "$dateFromParts", NULL, &ParseDollarDateFromParts,
	  &HandlePreParsedDollarDateFromParts, FEATURE_AGG_OPERATOR_DATEFROMPARTS },
	{ "$dateFromString", NULL, &ParseDollarDateFromString,
	  &HandlePreParsedDollarDateFromString, FEATURE_AGG_OPERATOR_DATEFROMSTRING },
	{ "$dateSubtract", NULL, &ParseDollarDateSubtract, &HandlePreParsedDollarDateSubtract,
	  FEATURE_AGG_OPERATOR_DATESUBTRACT },
	{ "$dateToParts", &HandleDollarDateToParts, NULL, NULL,
	  FEATURE_AGG_OPERATOR_DATETOPARTS },
	{ "$dateToString", &HandleDollarDateToString, NULL, NULL,
	  FEATURE_AGG_OPERATOR_DATETOSTRING },
	{ "$dateTrunc", NULL, &ParseDollarDateTrunc, &HandlePreParsedDollarDateTrunc,
	  FEATURE_AGG_OPERATOR_DATETRUNC },
	{ "$dayOfMonth", &HandleDollarDayOfMonth, NULL, NULL,
	  FEATURE_AGG_OPERATOR_DAYOFMONTH },
	{ "$dayOfWeek", &HandleDollarDayOfWeek, NULL, NULL, FEATURE_AGG_OPERATOR_DAYOFWEEK },
	{ "$dayOfYear", &HandleDollarDayOfYear, NULL, NULL, FEATURE_AGG_OPERATOR_DAYOFYEAR },
	{ "$degreesToRadians", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_DEGREESTORADIANS },
	{ "$divide", &HandleDollarDivide, NULL, NULL, FEATURE_AGG_OPERATOR_DIVIDE },
	{ "$eq", NULL, &ParseDollarEq, &HandlePreParsedDollarEq, FEATURE_AGG_OPERATOR_EQ },
	{ "$exp", &HandleDollarExp, NULL, NULL, FEATURE_AGG_OPERATOR_EXP },
	{ "$filter", NULL, &ParseDollarFilter, &HandlePreParsedDollarFilter,
	  FEATURE_AGG_OPERATOR_FILTER },
	{ "$first", &HandleDollarFirst, NULL, NULL, FEATURE_AGG_OPERATOR_FIRST },
	{ "$firstN", NULL, &ParseDollarFirstN, &HandlePreParsedDollarFirstN,
	  FEATURE_AGG_OPERATOR_FIRSTN },
	{ "$floor", &HandleDollarFloor, NULL, NULL, FEATURE_AGG_OPERATOR_FLOOR },
	{ "$function", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_FUNCTION },
	{ "$getField", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_GETFIELD },
	{ "$gt", NULL, &ParseDollarGt, &HandlePreParsedDollarGt, FEATURE_AGG_OPERATOR_GT },
	{ "$gte", NULL, &ParseDollarGte, &HandlePreParsedDollarGte,
	  FEATURE_AGG_OPERATOR_GTE },
	{ "$hour", &HandleDollarHour, NULL, NULL, FEATURE_AGG_OPERATOR_HOUR },
	{ "$ifNull", &HandleDollarIfNull, NULL, NULL, FEATURE_AGG_OPERATOR_IFNULL },
	{ "$in", &HandleDollarIn, NULL, NULL, FEATURE_AGG_OPERATOR_IN },
	{ "$indexOfArray", NULL, &ParseDollarIndexOfArray, &HandlePreParsedDollarIndexOfArray,
	  FEATURE_AGG_OPERATOR_INDEXOFARRAY },
	{ "$indexOfBytes", &HandleDollarIndexOfBytes, NULL, NULL,
	  FEATURE_AGG_OPERATOR_INDEXOFBYTES },
	{ "$indexOfCP", &HandleDollarIndexOfCP, NULL, NULL, FEATURE_AGG_OPERATOR_INDEXOFCP },
	{ "$isArray", &HandleDollarIsArray, NULL, NULL, FEATURE_AGG_OPERATOR_ISARRAY },
	{ "$isNumber", &HandleDollarIsNumber, NULL, NULL, FEATURE_AGG_OPERATOR_ISNUMBER },
	{ "$isoDayOfWeek", &HandleDollarIsoDayOfWeek, NULL, NULL,
	  FEATURE_AGG_OPERATOR_ISODAYOFWEEK },
	{ "$isoWeek", &HandleDollarIsoWeek, NULL, NULL, FEATURE_AGG_OPERATOR_ISOWEEK },
	{ "$isoWeekYear", &HandleDollarIsoWeekYear, NULL, NULL,
	  FEATURE_AGG_OPERATOR_ISOWEEKYEAR },
	{ "$last", &HandleDollarLast, NULL, NULL, FEATURE_AGG_OPERATOR_LAST },
	{ "$lastN", NULL, &ParseDollarLastN, &HandlePreParsedDollarLastN,
	  FEATURE_AGG_OPERATOR_LASTN },
	{ "$let", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_LET },
	{ "$literal", NULL, &ParseDollarLiteral, NULL, FEATURE_AGG_OPERATOR_LITERAL },
	{ "$ln", &HandleDollarLn, NULL, NULL, FEATURE_AGG_OPERATOR_LN },
	{ "$log", &HandleDollarLog, NULL, NULL, FEATURE_AGG_OPERATOR_LOG },
	{ "$log10", &HandleDollarLog10, NULL, NULL, FEATURE_AGG_OPERATOR_LOG10 },
	{ "$lt", NULL, &ParseDollarLt, &HandlePreParsedDollarLt, FEATURE_AGG_OPERATOR_LT },
	{ "$lte", NULL, &ParseDollarLte, &HandlePreParsedDollarLte,
	  FEATURE_AGG_OPERATOR_LTE },
	{ "$ltrim", &HandleDollarLtrim, NULL, NULL, FEATURE_AGG_OPERATOR_LTRIM },
	{ "$makeArray", NULL, &ParseDollarMakeArray, &HandlePreParsedDollarMakeArray,
	  FEATURE_AGG_OPERATOR_MAKE_ARRAY },
	{ "$map", NULL, &ParseDollarMap, &HandlePreParsedDollarMap,
	  FEATURE_AGG_OPERATOR_MAP },
	{ "$max", NULL, &ParseDollarMax, &HandlePreParsedDollarMax,
	  FEATURE_AGG_OPERATOR_MAX },
	{ "$maxN", NULL, &ParseDollarMaxN, &HandlePreParsedDollarMaxMinN,
	  FEATURE_AGG_OPERATOR_MAXN },
	{ "$mergeObjects", &HandleDollarMergeObjects, NULL, NULL,
	  FEATURE_AGG_OPERATOR_MERGEOBJECTS },
	{ "$meta", &HandleDollarMeta, NULL, NULL, FEATURE_AGG_OPERATOR_META },
	{ "$millisecond", &HandleDollarMillisecond, NULL, NULL,
	  FEATURE_AGG_OPERATOR_MILLISECOND },

	{ "$min", NULL, &ParseDollarMin, &HandlePreParsedDollarMin,
	  FEATURE_AGG_OPERATOR_MIN },
	{ "$minN", NULL, &ParseDollarMinN, &HandlePreParsedDollarMaxMinN,
	  FEATURE_AGG_OPERATOR_MINN },
	{ "$minute", &HandleDollarMinute, NULL, NULL, FEATURE_AGG_OPERATOR_MINUTE },
	{ "$mod", &HandleDollarMod, NULL, NULL, FEATURE_AGG_OPERATOR_MOD },
	{ "$month", &HandleDollarMonth, NULL, NULL, FEATURE_AGG_OPERATOR_MONTH },
	{ "$multiply", &HandleDollarMultiply, NULL, NULL, FEATURE_AGG_OPERATOR_MULTIPLY },
	{ "$ne", NULL, &ParseDollarNe, &HandlePreParsedDollarNe, FEATURE_AGG_OPERATOR_NE },
	{ "$not", &HandleDollarNot, NULL, NULL, FEATURE_AGG_OPERATOR_NOT },
	{ "$objectToArray", &HandleDollarObjectToArray, NULL, NULL,
	  FEATURE_AGG_OPERATOR_OBJECTTOARRAY },
	{ "$or", &HandleDollarOr, NULL, NULL, FEATURE_AGG_OPERATOR_OR },
	{ "$pow", &HandleDollarPow, NULL, NULL, FEATURE_AGG_OPERATOR_POW },
	{ "$push", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_PUSH },
	{ "$radiansToDegrees", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_RADIANSTODEGREES },
	{ "$rand", &HandleDollarRand, NULL, NULL, FEATURE_AGG_OPERATOR_RAND },
	{ "$range", NULL, &ParseDollarRange, &HandlePreParsedDollarRange,
	  FEATURE_AGG_OPERATOR_RANGE },
	{ "$reduce", NULL, &ParseDollarReduce, &HandlePreParsedDollarReduce,
	  FEATURE_AGG_OPERATOR_REDUCE },
	{ "$regexFind", NULL, &ParseDollarRegexFind, &HandlePreParsedDollarRegexFind,
	  FEATURE_AGG_OPERATOR_REGEXFIND },
	{ "$regexFindAll", NULL, &ParseDollarRegexFindAll, &HandlePreParsedDollarRegexFindAll,
	  FEATURE_AGG_OPERATOR_REGEXFINDALL },
	{ "$regexMatch", NULL, &ParseDollarRegexMatch, &HandlePreParsedDollarRegexMatch,
	  FEATURE_AGG_OPERATOR_REGEXMATCH },
	{ "$replaceAll", NULL, &ParseDollarReplaceAll, &HandlePreParsedDollarReplaceAll,
	  FEATURE_AGG_OPERATOR_REPLACEALL },
	{ "$replaceOne", NULL, &ParseDollarReplaceOne, &HandlePreParsedDollarReplaceOne,
	  FEATURE_AGG_OPERATOR_REPLACEONE },
	{ "$reverseArray", NULL, &ParseDollarReverseArray, HandlePreParsedDollarReverseArray,
	  FEATURE_AGG_OPERATOR_REVERSEARRAY },
	{ "$round", &HandleDollarRound, NULL, NULL, FEATURE_AGG_OPERATOR_ROUND },
	{ "$rtrim", &HandleDollarRtrim, NULL, NULL, FEATURE_AGG_OPERATOR_RTRIM },
	{ "$second", &HandleDollarSecond, NULL, NULL, FEATURE_AGG_OPERATOR_SECOND },
	{ "$setDifference", &HandleDollarSetDifference, NULL, NULL,
	  FEATURE_AGG_OPERATOR_SETDIFFERENCE },
	{ "$setEquals", &HandleDollarSetEquals, NULL, NULL, FEATURE_AGG_OPERATOR_SETEQUALS },
	{ "$setField", NULL, &ParseDollarSetField, &HandlePreParsedDollarSetField,
	  FEATURE_AGG_OPERATOR_SETFIELD },
	{ "$setIntersection", &HandleDollarSetIntersection, NULL, NULL,
	  FEATURE_AGG_OPERATOR_SETINTERSECTION },
	{ "$setIsSubset", &HandleDollarSetIsSubset, NULL, NULL,
	  FEATURE_AGG_OPERATOR_SETISSUBSET },
	{ "$setUnion", &HandleDollarSetUnion, NULL, NULL, FEATURE_AGG_OPERATOR_SETUNION },
	{ "$sin", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_SIN },
	{ "$sinh", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_SINH },
	{ "$size", &HandleDollarSize, NULL, NULL, FEATURE_AGG_OPERATOR_SIZE },
	{ "$slice", &HandleDollarSlice, NULL, NULL, FEATURE_AGG_OPERATOR_SLICE },
	{ "$sortArray", NULL, &ParseDollarSortArray, &HandlePreParsedDollarSortArray,
	  FEATURE_AGG_OPERATOR_SORTARRAY },
	{ "$split", &HandleDollarSplit, NULL, NULL, FEATURE_AGG_OPERATOR_SPLIT },
	{ "$sqrt", &HandleDollarSqrt, NULL, NULL, FEATURE_AGG_OPERATOR_SQRT },
	{ "$stdDevPop", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_STDDEVPOP },
	{ "$stdDevSamp", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_STDDEVSAMP },
	{ "$strLenBytes", &HandleDollarStrLenBytes, NULL, NULL,
	  FEATURE_AGG_OPERATOR_STRLENBYTES },
	{ "$strLenCP", &HandleDollarStrLenCP, NULL, NULL, FEATURE_AGG_OPERATOR_STRLENCP },
	{ "$strcasecmp", &HandleDollarStrCaseCmp, NULL, NULL,
	  FEATURE_AGG_OPERATOR_STRCASECMP },
	{ "$substr", NULL, &ParseDollarSubstrBytes, &HandlePreParsedDollarSubstrBytes,
	  FEATURE_AGG_OPERATOR_SUBSTR }, /* MongoDB treats $substr the same as $substrBytes, including error messages */
	{ "$substrBytes", NULL, &ParseDollarSubstrBytes, &HandlePreParsedDollarSubstrBytes,
	  FEATURE_AGG_OPERATOR_SUBSTRBYTES },
	{ "$substrCP", NULL, &ParseDollarSubstrCP, &HandlePreParsedDollarSubstrCP,
	  FEATURE_AGG_OPERATOR_SUBSTRCP },
	{ "$subtract", &HandleDollarSubtract, NULL, NULL, FEATURE_AGG_OPERATOR_SUBTRACT },
	{ "$sum", NULL, &ParseDollarSum, &HandlePreParsedDollarSum,
	  FEATURE_AGG_OPERATOR_SUM },
	{ "$switch", &HandleDollarSwitch, NULL, NULL, FEATURE_AGG_OPERATOR_SWITCH },
	{ "$tan", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_TAN },
	{ "$tanh", NULL, NULL, NULL, FEATURE_AGG_OPERATOR_TANH },
	{ "$toBool", &HandleDollarToBool, NULL, NULL, FEATURE_AGG_OPERATOR_TOBOOL },
	{ "$toDate", &HandleDollarToDate, NULL, NULL, FEATURE_AGG_OPERATOR_TODATE },
	{ "$toDecimal", &HandleDollarToDecimal, NULL, NULL, FEATURE_AGG_OPERATOR_TODECIMAL },
	{ "$toDouble", &HandleDollarToDouble, NULL, NULL, FEATURE_AGG_OPERATOR_TODOUBLE },
	{ "$toInt", &HandleDollarToInt, NULL, NULL, FEATURE_AGG_OPERATOR_TOINT },
	{ "$toLong", &HandleDollarToLong, NULL, NULL, FEATURE_AGG_OPERATOR_TOLONG },
	{ "$toLower", &HandleDollarToLower, NULL, NULL, FEATURE_AGG_OPERATOR_TOLOWER },
	{ "$toObjectId", &HandleDollarToObjectId, NULL, NULL,
	  FEATURE_AGG_OPERATOR_TOOBJECTID },
	{ "$toString", &HandleDollarToString, NULL, NULL, FEATURE_AGG_OPERATOR_TOSTRING },
	{ "$toUpper", &HandleDollarToUpper, NULL, NULL, FEATURE_AGG_OPERATOR_TOUPPER },
	{ "$trim", &HandleDollarTrim, NULL, NULL, FEATURE_AGG_OPERATOR_TRIM },
	{ "$trunc", &HandleDollarTrunc, NULL, NULL, FEATURE_AGG_OPERATOR_TRUNC },
	{ "$tsIncrement", NULL, &ParseDollarTsIncrement, &HandlePreParsedDollarTsIncrement,
	  FEATURE_AGG_OPERATOR_TSINCREMENT },
	{ "$tsSecond", NULL, &ParseDollarTsSecond, &HandlePreParsedDollarTsSecond,
	  FEATURE_AGG_OPERATOR_TSSECOND },
	{ "$type", &HandleDollarType, NULL, NULL, FEATURE_AGG_OPERATOR_TYPE },
	{ "$week", &HandleDollarWeek, NULL, NULL, FEATURE_AGG_OPERATOR_WEEK },
	{ "$year", &HandleDollarYear, NULL, NULL, FEATURE_AGG_OPERATOR_YEAR },
	{ "$zip", NULL, &ParseDollarZip, &HandlePreParsedDollarZip, FEATURE_AGG_OPERATOR_ZIP }
};

static int NumberOfOperatorExpressions = sizeof(OperatorExpressions) /
										 sizeof(MongoOperatorExpression);

/* The variable ROOT */
static const StringView RootVariableName =
{
	.length = 4,
	.string = "ROOT"
};

/* The variable REMOVE */
static const StringView RemoveVariableName =
{
	.length = 6,
	.string = "REMOVE"
};

static const StringView CurrentVariableName =
{
	.length = 7,
	.string = "CURRENT"
};

/*
 * Convenience method to create a simple ExpressionResult with the
 * specified LifetimeTracker.
 */
inline static ExpressionResult
ExpressionResultCreateWithTracker(ExpressionLifetimeTracker *tracker)
{
	ExpressionResultPrivate private;
	memset(&private, 0, sizeof(ExpressionResultPrivate));
	private.tracker = tracker;

	ExpressionResult child = { { 0 }, false, false, private };
	return child;
}


/*
 * Initializes an ExpressionResult based on a pre-defined element_writer
 */
inline static ExpressionResult
ExpressionResultCreateFromElementWriter(pgbson_element_writer *writer,
										ExpressionLifetimeTracker *tracker,
										const ExpressionVariableContext *
										parentVariableContext)
{
	ExpressionResult context = ExpressionResultCreateWithTracker(tracker);
	context.expressionResultPrivate.variableContext.parent = parentVariableContext;
	context.isExpressionWriter = true;
	context.expressionResultPrivate.writer = *writer;
	return context;
}


/*
 * Creates a hash table that stores a  entries using
 * a hash and search based on the element path.
 */
inline static HTAB *
CreateVariableEntryHashTable()
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(VariableData),
		sizeof(VariableData),
		VariableHashEntryCompareFunc,
		VariableHashEntryHashFunc
		);

	/* Create it of size 5 since usually the number of variables used is small, we can adjust if needed later on. */
	HTAB *variableElementHashSet =
		hash_create("Variable Hash Table", 5, &hashInfo, DefaultExtensionHashFlags);

	return variableElementHashSet;
}


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_expression_get);
PG_FUNCTION_INFO_V1(bson_expression_map);

/*
 * bson_expression_get evaluates a bson expression from a given
 * document. This follows operator expressions as per mongo aggregation expressions.
 * The input is expected to be a single value bson document
 * e.g. { "sum": "$a.b"}
 * The output is a bson document that contains the evaluation of that field
 * e.g. { "sum": [ 1, 2, 3 ] }
 *
 * If nullOnEmpty parameter is set, "null" is written when a path in the expression
 * is not found in the input document. Following is an example showing why we need
 * this distinction. The caller (Gateway) is expected to set it correctly based on
 * desired behavior.
 *
 *      input doc:  {}
 *
 *      $group spec:  {$group : { "key" : $a}}
 *      $project spec:  {$project : { "key" : $a}}
 *
 *      $group result:  {"key" : null}
 *      $project result:  {}
 */
Datum
bson_expression_get(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *expression = PG_GETARG_PGBSON(1);
	bool isNullOnEmpty = PG_GETARG_BOOL(2);
	ExpressionVariableContext *variableContext = NULL;
	pgbsonelement expressionElement;
	pgbson_writer writer;

	AggregationExpressionData expressionData;
	memset(&expressionData, 0, sizeof(AggregationExpressionData));

	PgbsonToSinglePgbsonElement(expression, &expressionElement);

	const AggregationExpressionData *state;
	const int argPosition = 1;
	SetCachedFunctionState(
		state,
		AggregationExpressionData,
		argPosition,
		ParseAggregationExpressionData,
		&expressionElement.bsonValue,
		variableContext);

	if (state == NULL)
	{
		ParseAggregationExpressionData(&expressionData, &expressionElement.bsonValue,
									   variableContext);
		state = &expressionData;
	}

	StringView path = {
		.length = expressionElement.pathLength,
		.string = expressionElement.path,
	};

	PgbsonWriterInit(&writer);
	EvaluateAggregationExpressionDataToWriter(state, document, path, &writer,
											  variableContext, isNullOnEmpty);

	pgbson *returnedBson = PgbsonWriterGetPgbson(&writer);

	PG_FREE_IF_COPY(document, 0);
	PG_RETURN_POINTER(returnedBson);
}


/*
 * bson_expression_map evaluates a bson expression from a given array of
 * document. This follows operator expressions as per mongo aggregation expressions.
 * The input document should be a document containing the given field name that
 * points to an array of documents.
 * e.g. { "array": [{"_id": 1 ...}, {"_id": 2 ...}, ...]}
 * The expression is expected to be a single value bson document
 * e.g. { "sum": "$a.b"}
 * The output is a bson document that contains the evaluation of that field
 * e.g. { "sum": [ 1, 2, 3 ] }
 *
 * If nullOnEmpty parameter is set, "null" is written when a path in the expression
 * is not found in the input document. If nullOnEmpty parameter is not set the output
 * array will be missing results for those entities and the size of the output array
 * will not match the size of the input array.
 */
Datum
bson_expression_map(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	char *srcArrayName = text_to_cstring(PG_GETARG_TEXT_P(1));
	pgbson *expression = PG_GETARG_PGBSON_PACKED(2);
	bool isNullOnEmpty = PG_GETARG_BOOL(3);
	ExpressionVariableContext *variableContext = NULL;
	pgbsonelement expressionElement;
	pgbson_writer writer;
	pgbson_array_writer arrayWriter;

	AggregationExpressionData expressionData;
	memset(&expressionData, 0, sizeof(AggregationExpressionData));

	PgbsonToSinglePgbsonElement(expression, &expressionElement);
	bson_iter_t docIter;
	PgbsonInitIterator(document, &docIter);
	const bson_value_t *inputArray = NULL;

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, srcArrayName) == 0)
		{
			inputArray = bson_iter_value(&docIter);
			break;
		}
	}

	/* Missing input Array */
	if (inputArray == NULL)
	{
		ereport(ERROR, errcode(ERRCODE_INTERNAL_ERROR), errmsg(
					"Missing Input Array for bson_expression_map: '%s'", srcArrayName));
	}
	else if (inputArray->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, errcode(ERRCODE_INTERNAL_ERROR), errmsg(
					"Input Array for bson_express_map of wrong type Name: '%s' Type: '%s'",
					srcArrayName, BsonTypeName(inputArray->value_type)));
	}

	/* Init AggregateExpressionData */
	const AggregationExpressionData *state;
	const int argPosition = 2;

	SetCachedFunctionState(
		state,
		AggregationExpressionData,
		argPosition,
		ParseAggregationExpressionData,
		&expressionElement.bsonValue,
		variableContext);

	if (state == NULL)
	{
		ParseAggregationExpressionData(&expressionData, &expressionElement.bsonValue,
									   variableContext);
		state = &expressionData;
	}

	StringView path = {
		.length = expressionElement.pathLength,
		.string = expressionElement.path,
	};

	/* Perform expression mapping for each document in array and append result to a new array. */
	bson_iter_t arrayIter;
	BsonValueInitIterator(inputArray, &arrayIter);
	PgbsonWriterInit(&writer);
	PgbsonWriterStartArray(&writer, path.string, path.length, &arrayWriter);
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *docValue = bson_iter_value(&arrayIter);
		if (docValue->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, errcode(ERRCODE_INTERNAL_ERROR), errmsg(
						"Input Array for bson_expression_map does not contain Document. Type: '%s'",
						BsonTypeName(docValue->value_type)));
		}
		pgbson_writer docWriter;
		bson_iter_t innerIter;
		PgbsonWriterInit(&docWriter);

		EvaluateAggregationExpressionDataToWriter(state, PgbsonInitFromDocumentBsonValue(
													  docValue), path, &docWriter,
												  variableContext, isNullOnEmpty);
		PgbsonWriterGetIterator(&docWriter, &innerIter);
		if (bson_iter_next(&innerIter) && strncmp(bson_iter_key(&innerIter), path.string,
												  path.length) == 0)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, bson_iter_value(&innerIter));
		}
	}
	PgbsonWriterEndArray(&writer, &arrayWriter);

	pgbson *returnedBson = PgbsonWriterGetPgbson(&writer);

	PG_FREE_IF_COPY(document, 0);
	PG_RETURN_POINTER(returnedBson);
}


/*
 * Evaluates the expression described by expressionValue based on the input document.
 * If the expression evaluates to a value, writes it into the target writer at the specified field.
 */
void
EvaluateExpressionToWriter(pgbson *document, const pgbsonelement *expressionElement,
						   pgbson_writer *writer,
						   ExpressionVariableContext *variableContext, bool isNullOnEmpty)
{
	ExpressionLifetimeTracker tracker = { 0 };
	if (expressionElement->bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		pgbson_array_writer childArrayWriter;
		pgbson_element_writer childArrayElementWriter;
		bson_iter_t elementIterator;
		bson_iter_init_from_data(&elementIterator,
								 expressionElement->bsonValue.value.v_doc.data,
								 expressionElement->bsonValue.value.v_doc.data_len);
		PgbsonWriterStartArray(writer, expressionElement->path,
							   expressionElement->pathLength, &childArrayWriter);
		PgbsonInitArrayElementWriter(&childArrayWriter, &childArrayElementWriter);
		EvaluateExpressionArrayToWriter(document, &elementIterator,
										&childArrayElementWriter, &tracker,
										variableContext, isNullOnEmpty);
		PgbsonWriterEndArray(writer, &childArrayWriter);
	}
	else if (expressionElement->bsonValue.value_type == BSON_TYPE_DOCUMENT)
	{
		pgbson_element_writer objectElementWriter;
		PgbsonInitObjectElementWriter(writer, &objectElementWriter,
									  expressionElement->path,
									  expressionElement->pathLength);
		EvaluateExpressionObjectToWriter(document, &(expressionElement->bsonValue),
										 &objectElementWriter, &tracker, variableContext,
										 isNullOnEmpty);
	}
	else
	{
		pgbson_element_writer elementWriter;
		PgbsonInitObjectElementWriter(writer, &elementWriter, expressionElement->path,
									  expressionElement->pathLength);
		ExpressionResult expressionResult = ExpressionResultCreateFromElementWriter(
			&elementWriter, &tracker, variableContext);
		EvaluateExpression(document, &(expressionElement->bsonValue), &expressionResult,
						   isNullOnEmpty);
	}

	list_free_deep(tracker.itemsToFree);
}


/*
 *  Recursively evaluates expression of the form  <fieldName> :  { <field1> : <expression>, <field2>  : <expression>]
 *  e.g.,  "newField" :  { "id" : "$_id",  { "val" : {"a" : "b"}} }
 *
 *  Each element of the document can be one of the followings:
 *      1. an operator expression of the form  <operatorName> : <operatorExpression>
 *          e.g.,  $isArray : ["$a.b"]
 *      2. a general expression of the form  <fieldName> : <expression>, that needs to evaluated recursively.
 *
 *  Error cases:  If the expression specification has an operator (e.g., $isArray, $literal, $concatArray),
 *                it has to be the only field.
 *
 *          E.g.,  "expressionKey" : { $literal : 2.0}                      -> OK
 *          E.g.,  "expressionKey" : { $literal : 2.0, "a" : "b"}           -> NOT OK
 *          E.g.,  "expressionKey" : { "a" : 2.0, $isArray : "$b"}          -> NOT OK
 *          E.g.,  "expressionKey" : { $literal : 2.0, $isArray : "$b"}     -> NOT OK
 *          E.g.,  "expressionKey" : { $isArray : "$b"}                     -> OK
 *          E.g.,  "expressionKey" : { "c" : "$d", "a" : "b"}               -> OK
 *
 */
void
EvaluateExpressionObjectToWriter(pgbson *document, const bson_value_t *expressionValue,
								 pgbson_element_writer *elementWriter,
								 ExpressionLifetimeTracker *tracker,
								 ExpressionVariableContext *variableContext,
								 bool isNullOnEmpty)
{
	bson_iter_t elementIterator;
	pgbson_writer childWriter;
	pgbsonelement operatorElement;
	bool hasElements = false;
	const char *previousField = NULL;

	bson_iter_init_from_data(&elementIterator,
							 expressionValue->value.v_doc.data,
							 expressionValue->value.v_doc.data_len);

	/*
	 *  Case 1:  The specification document contains an Operator in the first element
	 *
	 *  Note: "expressionKey" : { $isArray : "$b"}    is needed to be evaluated as
	 *          either, "expressionKey" : true    (since, "expressionKey" : { true }  is not a valid document)
	 *          or,     "expressionKey" : false
	 *
	 *      Hence, we don't start a document by writing "{".  GetAndEvaluateOperator() will take care
	 *      if any document needs to be written .
	 */
	if (bson_iter_next(&elementIterator))
	{
		BsonIterToPgbsonElement(&elementIterator, &operatorElement);

		/* If the field is an operator */
		if (operatorElement.pathLength > 1 && operatorElement.path[0] == '$')
		{
			/*
			 *  If first field is an operator, we should not see any more field in the spec document.
			 *  e.g.,  throws error for this example => "expressionKey" : { $literal : 2.0, "a" : "b"}
			 */
			if (bson_iter_next(&elementIterator))
			{
				bool performOperatorCheck = false;
				ReportOperatorExpressonSyntaxError(operatorElement.path, &elementIterator,
												   performOperatorCheck);
			}

			ExpressionResult expressionResult = ExpressionResultCreateFromElementWriter(
				elementWriter, tracker, variableContext);
			GetAndEvaluateOperator(document, operatorElement.path,
								   &operatorElement.bsonValue, &expressionResult);

			return;
		}

		hasElements = true;
	}

	/*
	 *  Case 2:  The specification document does not contain an Operator in the first element
	 *
	 *  Note:
	 *  (1) We don't come here if the first field was an operator. We would have returned with
	 *  a success or an error.
	 *
	 *  (2) Since, an operator was not found, we can start writing a document. However, we might
	 *  be writing into an array or as the value of a field of an object.
	 */

	PgbsonElementWriterStartDocument(elementWriter, &childWriter);

	if (hasElements)
	{
		/*
		 *  bson_iter_next has already advanced to check for the operator, but didn't find one.
		 *  We will continue with the do-while loop since we already have a non-oprator element
		 *  at the current elementIterator.
		 */
		do {
			/* throw error if we find an operator at any point */
			bool performOperatorCheck = true;
			ReportOperatorExpressonSyntaxError(previousField, &elementIterator,
											   performOperatorCheck);

			/* Recursively evaluate the general expression */
			pgbsonelement element;
			BsonIterToPgbsonElement(&elementIterator, &element);
			EvaluateExpressionToWriter(document, &element, &childWriter, variableContext,
									   isNullOnEmpty);

			previousField = bson_iter_key(&elementIterator);
		} while (bson_iter_next(&elementIterator));
	}

	PgbsonElementWriterEndDocument(elementWriter, &childWriter);
}


/*
 *  Recursively evaluates expression of the form  <fieldName> :  [<expression>, <expression>]
 *  e.g.,  "newField" :  [
 *                          "$_id",                     // Example 1: field
 *                          { "val" : ["$a", "$b"]},    // Example 2: document (has non-operator fields)
 *                          ["$a", { "a" : "$b"}],      // Example 3: array
 *                          { $literal: [1,2] }         // Example 4: document (has operator)
 *                      ]
 *
 *  Each element of the array can be one of the followings:
 *      Case 1. document expression that needs to be evaluated recursively  (e.g., the 2nd and 4th array element)
 *      Case 2. array of expression that needs to be evaluated recursively  (e.g., the 3rd array element)
 *      Case 3. an expression that can be evaluated directly since it doesn't hold an arrary or a document (e.g., the 1st array element)
 */
void
EvaluateExpressionArrayToWriter(pgbson *document, bson_iter_t *elementIterator,
								pgbson_element_writer *elementWriter,
								ExpressionLifetimeTracker *tracker,
								ExpressionVariableContext *variableContext,
								bool isNullOnEmpty)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	while (bson_iter_next(elementIterator))
	{
		/* Case 1 */
		if (BSON_ITER_HOLDS_ARRAY(elementIterator))
		{
			pgbson_array_writer arrayELementWriter;
			pgbson_element_writer arrayElementWriter;
			bson_iter_t childObjIter;
			PgbsonArrayWriterStartArray(elementWriter->arrayWriter,
										&arrayELementWriter);
			PgbsonInitArrayElementWriter(&arrayELementWriter, &arrayElementWriter);
			if (bson_iter_recurse(elementIterator, &childObjIter))
			{
				EvaluateExpressionArrayToWriter(document, &childObjIter,
												&arrayElementWriter, tracker,
												variableContext, isNullOnEmpty);
			}

			PgbsonArrayWriterEndArray(elementWriter->arrayWriter,
									  &arrayELementWriter);
		}
		else
		{
			const bson_value_t *currentElement = bson_iter_value(elementIterator);

			pgbson_writer innerWriter;
			pgbson_element_writer innerElementWriter;
			PgbsonWriterInit(&innerWriter);
			PgbsonInitObjectElementWriter(&innerWriter, &innerElementWriter, "", 0);

			/* Case 2 */
			if (BSON_ITER_HOLDS_DOCUMENT(elementIterator))
			{
				EvaluateExpressionObjectToWriter(document, currentElement,
												 &innerElementWriter, tracker,
												 variableContext,
												 isNullOnEmpty);
			}
			/* Case 3 */
			else
			{
				ExpressionResult expressionResult =
					ExpressionResultCreateFromElementWriter(
						&innerElementWriter, tracker, variableContext);
				EvaluateExpression(document, currentElement, &expressionResult,
								   isNullOnEmpty);
			}

			bson_value_t writtenValue = PgbsonElementWriterGetValue(&innerElementWriter);

			/* For expressions nested in an array that evaluate to undefined we should write null. */
			if (IsExpressionResultUndefined(&writtenValue))
			{
				PgbsonArrayWriterWriteNull(elementWriter->arrayWriter);
			}
			else
			{
				PgbsonArrayWriterWriteValue(elementWriter->arrayWriter, &writtenValue);
			}

			PgbsonWriterFree(&innerWriter);
		}
	}
}


/*
 * Evaluate the expression specified in expressionValue and set the result of the evaluation
 * in the <see cref="ExpressionResult" />
 */
void
EvaluateExpression(pgbson *document, const bson_value_t *expressionValue,
				   ExpressionResult *expressionResult, bool isNullOnEmpty)
{
	switch (expressionValue->value_type)
	{
		case BSON_TYPE_UTF8:
		{
			const char *strValue = expressionValue->value.v_utf8.str;
			uint32_t strLen = expressionValue->value.v_utf8.len;

			if (strLen > 2 && (strncmp(strValue, "$$", 2) == 0))
			{
				StringView dottedExpression = {
					.length = strLen - 2,
					.string = strValue + 2
				};

				bool isDottedExpression = StringViewContains(&dottedExpression, '.');
				StringView varName = dottedExpression;

				if (isDottedExpression)
				{
					varName = StringViewFindPrefix(&dottedExpression, '.');
				}

				bson_value_t variableValue;
				if (!ExpressionResultGetVariable(varName,
												 expressionResult,
												 document,
												 &variableValue))
				{
					ereport(ERROR, (errcode(MongoLocation17276),
									errmsg("Use of undefined variable: %s",
										   CreateStringFromStringView(&varName))));
				}

				if (!isDottedExpression)
				{
					ExpressionResultSetValue(expressionResult, &variableValue);
					return;
				}

				/* Evaluate dotted expression for a variable */
				StringView varNameDottedSuffix = StringViewFindSuffix(
					&dottedExpression, '.');
				EvaluateFieldPathAndWrite(&variableValue,
										  varNameDottedSuffix.string,
										  varNameDottedSuffix.length,
										  ExpressionResultGetElementWriter(
											  expressionResult),
										  isNullOnEmpty);

				expressionResult->isFieldPathExpression = true;
				ExpressionResultSetValueFromWriter(expressionResult);
				return;
			}
			else if (strLen > 1 && strValue[0] == '$')
			{
				/* if the string starts with $ it's a special projection. Otherwise it's a const value.
				 * field path expressions should be treated as $$CURRENT.<path> */
				bson_value_t currentValue;

				/* should always return true since current defaults to root if not defined. */
				bool found PG_USED_FOR_ASSERTS_ONLY =
					ExpressionResultGetVariable(CurrentVariableName,
												expressionResult,
												document,
												&currentValue);

				Assert(found);

				EvaluateFieldPathAndWrite(&currentValue,
										  strValue + 1,
										  strLen - 1,
										  ExpressionResultGetElementWriter(
											  expressionResult),
										  isNullOnEmpty);

				expressionResult->isFieldPathExpression = true;
				ExpressionResultSetValueFromWriter(expressionResult);
			}
			else
			{
				ExpressionResultSetValue(expressionResult, expressionValue);
			}
			break;
		}

		case BSON_TYPE_DOCUMENT:
		{
			/* Evaluate nested expressions/operators in the document. */
			pgbson_element_writer *elementWriter =
				ExpressionResultGetElementWriter(expressionResult);
			EvaluateExpressionObjectToWriter(document, expressionValue,
											 elementWriter,
											 expressionResult->expressionResultPrivate.
											 tracker,
											 &expressionResult->expressionResultPrivate.
											 variableContext,
											 isNullOnEmpty);

			ExpressionResultSetValueFromWriter(expressionResult);

			break;
		}

		case BSON_TYPE_ARRAY:
		{
			bson_iter_t arrayIterator;
			BsonValueInitIterator(expressionValue, &arrayIterator);

			pgbson_array_writer childArrayWriter;
			pgbson_element_writer childArrayElementWriter;
			pgbson_element_writer *elementWriter = ExpressionResultGetElementWriter(
				expressionResult);
			PgbsonElementWriterStartArray(elementWriter, &childArrayWriter);
			PgbsonInitArrayElementWriter(&childArrayWriter, &childArrayElementWriter);
			EvaluateExpressionArrayToWriter(document, &arrayIterator,
											&childArrayElementWriter,
											expressionResult->expressionResultPrivate.
											tracker,
											&expressionResult->expressionResultPrivate.
											variableContext,
											isNullOnEmpty);
			PgbsonElementWriterEndArray(elementWriter, &childArrayWriter);
			ExpressionResultSetValueFromWriter(expressionResult);

			break;
		}

		default:
		{
			/* if it's not any special case, it's a static value, append it to the document. */
			ExpressionResultSetValue(expressionResult, expressionValue);
			break;
		}
	}
}


static bool
GetVariableValueFromData(const VariableData *variable, pgbson *currentDocument,
						 ExpressionResult *parentExpressionResult,
						 bson_value_t *variableValue)
{
	if (variable->isConstant)
	{
		*variableValue = variable->bsonValue;
	}
	else
	{
		bool isNullOnEmpty = false;
		ExpressionResult childExpressionResult = ExpressionResultCreateChild(
			parentExpressionResult);
		EvaluateAggregationExpressionData(variable->expression, currentDocument,
										  &childExpressionResult, isNullOnEmpty);
		*variableValue = childExpressionResult.value;
	}

	return true;
}


/*
 * Looks for a named variable in the variable context tree.
 * Returns true if the variable is found and its value is set to variableValue.
 * Returns false if the variable doesn't exist in the current context tree.
 */
static bool
ExpressionResultGetVariable(StringView variableName,
							ExpressionResult *expressionResult,
							pgbson *currentDocument,
							bson_value_t *variableValue)
{
	const ExpressionVariableContext *current =
		&expressionResult->expressionResultPrivate.variableContext;
	while (current != NULL)
	{
		if (current->hasSingleVariable)
		{
			StringView currentVariableName = current->context.variable.name;

			if (StringViewEquals(&variableName, &currentVariableName))
			{
				return GetVariableValueFromData(&current->context.variable,
												currentDocument, expressionResult,
												variableValue);
			}
		}
		else if (current->context.table != NULL)
		{
			VariableData entryToFind;
			entryToFind.name = variableName;

			bool found = false;
			VariableData *hashEntry = hash_search(current->context.table,
												  &entryToFind,
												  HASH_FIND, &found);

			if (found)
			{
				return GetVariableValueFromData(hashEntry, currentDocument,
												expressionResult, variableValue);
			}
		}

		current = current->parent;
	}

	/* Not found, try static well known variables */
	if (StringViewEquals(&variableName, &RootVariableName) &&
		currentDocument != NULL)
	{
		*variableValue = ConvertPgbsonToBsonValue(currentDocument);
		return true;
	}

	/* If we get here, $$CURRENT was not overriden, so we set $$CURRENT to $$ROOT
	 * TODO: Remove this once we move to the new framework.
	 */
	if (StringViewEquals(&variableName, &CurrentVariableName))
	{
		*variableValue = ConvertPgbsonToBsonValue(currentDocument);
		return true;
	}

	/* When variable name is $$REMOVE, we set the value to Bson EOD to represent a missing path expression. */
	if (StringViewEquals(&variableName, &RemoveVariableName))
	{
		bson_value_t value = {
			.value_type = BSON_TYPE_EOD
		};

		*variableValue = value;
		return true;
	}

	return false;
}


/*
 * Function to add a constant variable to the expression result context
 */
void
ExpressionResultSetConstantVariable(ExpressionResult *expressionResult, const
									StringView *variableName, const
									bson_value_t *value)
{
	VariableData newVariable = {
		.name = *variableName,
		.isConstant = true,
		.bsonValue = *value
	};

	VariableContextSetVariableData(
		&expressionResult->expressionResultPrivate.variableContext, &newVariable);
}


/*
 * Sets a variable into the expression result's variable context, with the given name and value.
 */
static void
VariableContextSetVariableData(ExpressionVariableContext *variableContext, const
							   VariableData *variableData)
{
	HTAB *hashTable = variableContext->context.table;

	if (hashTable == NULL && !variableContext->hasSingleVariable)
	{
		/* first variable added to the variable context, treat it as single variable to optimize for */
		/* operators/pipelines which just declare one variable. */
		variableContext->context.variable = *variableData;
		variableContext->hasSingleVariable = true;
		return;
	}

	if (variableContext->hasSingleVariable)
	{
		VariableData currentVariable = variableContext->context.variable;

		/* override single variable, no need to create the hash table. */
		if (StringViewEquals(&variableData->name, &currentVariable.name))
		{
			variableContext->context.variable = *variableData;
			return;
		}

		/* create the hashTable since it is a union in the struct with variable we can't check for null here
		 * but we know hashTable is null when hasSingleVariable == true. */
		hashTable = CreateVariableEntryHashTable();

		/* insert singleVariable to hash table. */
		InsertVariableToContextTable(&currentVariable, hashTable);
		variableContext->hasSingleVariable = false;
	}

	InsertVariableToContextTable(variableData, hashTable);
	variableContext->context.table = hashTable;
}


/* Helper function that validates variable name */
void
ValidateVariableName(StringView name)
{
	if (name.length <= 0)
	{
		ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
							"empty variable names are not allowed")));
	}

	uint32_t i;
	for (i = 0; i < name.length; i++)
	{
		char current = name.string[i];
		if (i == 0 && isascii(current) && !islower(current) &&
			!IsOverridableSystemVariable(&name))
		{
			ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
								"'%s' starts with an invalid character for a user variable name",
								name.string)));
		}
		else if (isascii(current) && !isdigit(current) && !islower(current) &&
				 !isupper(current) && current != '_')
		{
			ereport(ERROR, (errcode(MongoFailedToParse), errmsg(
								"'%s' contains an invalid character for a variable name: '%c'",
								name.string, current)));
		}
	}
}


/* Helper function that checks if a variable name is an overridable system variable. */
static bool
IsOverridableSystemVariable(StringView *name)
{
	/* Only CURRENT is overridable so far. */
	return StringViewEquals(name, &CurrentVariableName);
}


/* Inserts/overrides an element into the variable hash table. */
static void
InsertVariableToContextTable(const VariableData *variableElement, HTAB *hashTable)
{
	bool found = false;
	VariableData *hashEntry = hash_search(hashTable, variableElement,
										  HASH_ENTER, &found);

	if (found)
	{
		*hashEntry = *variableElement;
	}
}


/*
 * Helper function that creates a child expression result, evaluates the given expression into it and returns the value from the evaluation.
 */
bson_value_t
EvaluateExpressionAndGetValue(pgbson *doc, const bson_value_t *expression,
							  ExpressionResult *expressionResult, bool isNullOnEmpty)
{
	ExpressionResult childExpressionResult =
		ExpressionResultCreateChild(expressionResult);

	EvaluateExpression(doc, expression, &childExpressionResult, isNullOnEmpty);

	return childExpressionResult.value;
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

static inline void
ReportOperatorExpressonSyntaxError(const char *fieldA, bson_iter_t *fieldBIter, bool
								   performOperatorCheck)
{
	if (bson_iter_key_len(fieldBIter) == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"FieldPath cannot be constructed with empty string.")));
	}

	/* 1. If performOperatorCheck = false, time to throw error already */
	/* 2. Or, see if the iterator is pointing to an operator. If yes, throw error. */
	if (!performOperatorCheck || (bson_iter_key_len(fieldBIter) > 1 && bson_iter_key(
									  fieldBIter)[0] == '$'))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"an expression operator specification must contain exactly one field, found 2 fields '%s' and '%s'.",
							fieldA,
							bson_iter_key(fieldBIter))));
	}
}


/*
 * Given an expression of the form "field": { "$operator": <expression> }
 * Detects the operator from the first element and evaluates the expression into
 * a constant value for a specified document.
 * Supported operators are defined in OperatorExpressions[] above.
 */
static void
GetAndEvaluateOperator(pgbson *document,
					   const char *operatorName,
					   const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult)
{
	MongoOperatorExpression searchKey = {
		.operatorName = operatorName,
		.legacyHandleOperatorFunc = NULL
	};

	MongoOperatorExpression *pItem = (MongoOperatorExpression *) bsearch(&searchKey,
																		 OperatorExpressions,
																		 NumberOfOperatorExpressions,
																		 sizeof(
																			 MongoOperatorExpression),
																		 CompareOperatorExpressionByName);

	if (pItem == NULL)
	{
		ereport(ERROR, (errcode(MongoLocation31325), errmsg(
							"Unknown expression %s", searchKey.operatorName)));
	}

	if (pItem->parseAggregationExpressionFunc != NULL)
	{
		/* Support for the preparsed expression engine when an operator implements it and is nested in an operator that doesn't implement it. */
		/* This is a temp workaround until all operators implement the new preparsed framework, once all operators move, this will be removed. */
		AggregationExpressionData *expressionData =
			palloc0(sizeof(AggregationExpressionData));
		expressionData->kind = AggregationExpressionKind_Operator;
		pItem->parseAggregationExpressionFunc(operatorValue, expressionData,
											  &expressionResult->expressionResultPrivate.
											  variableContext);

		/* If it was not optimized to a constant when parsing, call the handler and free the arguments allocated memory. */
		if (expressionData->kind == AggregationExpressionKind_Operator)
		{
			Assert(pItem->handlePreParsedOperatorFunc != NULL);

			pItem->handlePreParsedOperatorFunc(document,
											   expressionData->operator.arguments,
											   expressionResult);

			switch (expressionData->operator.argumentsKind)
			{
				case AggregationExpressionArgumentsKind_List:
				{
					list_free_deep(expressionData->operator.arguments);
					break;
				}

				case AggregationExpressionArgumentsKind_Palloc:
				{
					pfree(expressionData->operator.arguments);
					break;
				}

				default:
				{
					ereport(ERROR, (errcode(MongoBadValue), errmsg(
										"Unexpected aggregation expression argument kind after evaluating a pre-parse operator.")));
				}
			}
		}
		else
		{
			bool isNullOnEmpty = false;
			EvaluateAggregationExpressionData(expressionData, document, expressionResult,
											  isNullOnEmpty);
		}

		pfree(expressionData);
	}
	else if (pItem->legacyHandleOperatorFunc != NULL)
	{
		pItem->legacyHandleOperatorFunc(document, operatorValue, expressionResult);
	}
	else
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("Operator %s not implemented yet", operatorName),
						errhint("Operator %s not implemented yet", operatorName)));
	}
}


static int
CompareOperatorExpressionByName(const void *a, const void *b)
{
	return strcmp((*(MongoOperatorExpression *) a).operatorName,
				  (*(MongoOperatorExpression *) b).operatorName);
}


/*
 * Given an expression of the form "field": "$dotted.path.to.field"
 * Walks the document to find the instances of the dottedPathExpression
 * and projects them into the writer with the $field projection semantics.
 */
static bool
EvaluateFieldPathAndWriteCore(bson_iter_t *document, const char *dottedPathExpression,
							  uint32_t dottedPathExpressionLength,
							  pgbson_element_writer *writer, bool isNullOnEmpty)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	/*
	 * If the expression is matches into an array,
	 * we don't need to write null for paths not found.
	 *
	 *  doc1: { a : [ 1, 2, 3]}
	 *  doc2: { a : [ 1, 2, {b : 9}]}
	 *
	 *  expression: { "field" : "$a.b"}
	 *
	 *  The results are
	 *  output1 : { "field" : [] }
	 *  output2 : { "field" : [9] }
	 */
	bool isNullOnEmptyWhenDocumentHasArray = false;

	const char *dotKeyStr = memchr(dottedPathExpression, '.', dottedPathExpressionLength);
	if (dotKeyStr == NULL)
	{
		if (!bson_iter_find_w_len(document, dottedPathExpression,
								  dottedPathExpressionLength))
		{
			if (isNullOnEmpty)
			{
				bson_value_t nullValue = { 0 };
				nullValue.value_type = BSON_TYPE_NULL;
				PgbsonElementWriterWriteValue(writer, &nullValue);
				return true;
			}

			return false;
		}

		/* field found, copy value. */
		PgbsonElementWriterWriteValue(writer, bson_iter_value(document));
		return true;
	}

	uint32_t currentFieldLength = dotKeyStr - dottedPathExpression;
	if (!bson_iter_find_w_len(document, dottedPathExpression, currentFieldLength))
	{
		if (isNullOnEmpty)
		{
			bson_value_t nullValue = { 0 };
			nullValue.value_type = BSON_TYPE_NULL;
			PgbsonElementWriterWriteValue(writer, &nullValue);
			return true;
		}

		return false;
	}

	const char *remainingPath = dotKeyStr + 1;
	uint32_t remainingPathLength = dottedPathExpressionLength - currentFieldLength - 1;
	if (BSON_ITER_HOLDS_DOCUMENT(document))
	{
		bson_iter_t childObjIterator;
		if (bson_iter_recurse(document, &childObjIterator))
		{
			return EvaluateFieldPathAndWriteCore(&childObjIterator, remainingPath,
												 remainingPathLength, writer,
												 isNullOnEmpty);
		}
	}
	else if (BSON_ITER_HOLDS_ARRAY(document))
	{
		bson_iter_t childArrayIterator;
		if (bson_iter_recurse(document, &childArrayIterator))
		{
			/* write an array into the element */
			pgbson_array_writer arrayWriter;
			pgbson_element_writer innerWriter;
			PgbsonElementWriterStartArray(writer, &arrayWriter);
			PgbsonInitArrayElementWriter(&arrayWriter, &innerWriter);
			while (bson_iter_next(&childArrayIterator))
			{
				bson_iter_t nestedDocumentIter;
				if (BSON_ITER_HOLDS_DOCUMENT(&childArrayIterator) &&
					bson_iter_recurse(&childArrayIterator, &nestedDocumentIter))
				{
					EvaluateFieldPathAndWriteCore(&nestedDocumentIter, remainingPath,
												  remainingPathLength, &innerWriter,
												  isNullOnEmptyWhenDocumentHasArray);
				}
			}

			PgbsonElementWriterEndArray(writer, &arrayWriter);
			return true;
		}
	}

	return false;
}


/*
 * Given an expression of the form "field": "path.to.field" and a given bson value
 * walks the value if it holds a document or an array to find the instance of the dotted expression.
 */
static bool
EvaluateFieldPathAndWrite(bson_value_t *value, const
						  char *dottedPathExpression,
						  uint32_t dottedPathExpressionLength,
						  pgbson_element_writer *writer, bool isNullOnEmpty)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	/*
	 * If the variable is matches into an array,
	 * we don't need to write null for paths not found.
	 *
	 *  when $$value points to: [ 1, 2, {b : 9}]
	 *
	 *  expression: { "field" : "$$value.b"}
	 *
	 *  The results is
	 *  output : { "field" : [9] }
	 */
	uint32_t remainingPathLength = dottedPathExpressionLength;
	bson_iter_t valueIter;
	BsonValueInitIterator(value, &valueIter);
	if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		return EvaluateFieldPathAndWriteCore(&valueIter, dottedPathExpression,
											 remainingPathLength, writer, isNullOnEmpty);
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		/* write an array into the element */
		pgbson_array_writer arrayWriter;
		pgbson_element_writer innerWriter;
		PgbsonElementWriterStartArray(writer, &arrayWriter);
		PgbsonInitArrayElementWriter(&arrayWriter, &innerWriter);
		while (bson_iter_next(&valueIter))
		{
			bson_iter_t nestedDocumentIter;
			if (BSON_ITER_HOLDS_DOCUMENT(&valueIter) &&
				bson_iter_recurse(&valueIter, &nestedDocumentIter))
			{
				EvaluateFieldPathAndWriteCore(&nestedDocumentIter, dottedPathExpression,
											  remainingPathLength, &innerWriter,
											  isNullOnEmpty);
			}
		}

		PgbsonElementWriterEndArray(writer, &arrayWriter);
		return true;
	}

	return false;
}


/* --------------------------------------------------------- */
/* ExpressionResult functions */
/* --------------------------------------------------------- */

/*
 * Sets the value specified as the result of the Expression.
 * This handles the various states of the result and sets
 * the appropriate result. For writer mode results, writes to the
 * initialized writers. Otherwise updates the value to the specified
 * bson_value_t.
 */
void
ExpressionResultSetValue(ExpressionResult *expressionResult, const
						 bson_value_t *value)
{
	/* if there is a writer, write to it. */
	if (expressionResult->isExpressionWriter)
	{
		PgbsonElementWriterWriteValue(&expressionResult->expressionResultPrivate.writer,
									  value);
	}
	else
	{
		expressionResult->expressionResultPrivate.valueSet = true;
		expressionResult->value = *value;
	}

	if (!expressionResult->expressionResultPrivate.variableContext.hasSingleVariable)
	{
		hash_destroy(
			expressionResult->expressionResultPrivate.variableContext.context.table);
		expressionResult->expressionResultPrivate.variableContext.context.table = NULL;
	}
}


/*
 * Finalizes and sets the result value from the expression evaluation
 * to the top level value to be accessed by callers.
 */
void
ExpressionResultSetValueFromWriter(ExpressionResult *expressionResult)
{
	if (!expressionResult->isExpressionWriter)
	{
		ereport(ERROR, (errmsg(
							"Unable to set value for expression from writer when writer does not exist")));
	}

	if (expressionResult->expressionResultPrivate.valueSet)
	{
		ereport(ERROR, (errmsg(
							"Cannot call ExpressionResultSetValueFromWriter multiple times")));
	}

	/* First we extract the value from the element writer */
	const bson_value_t bsonValue = PgbsonElementWriterGetValue(
		&expressionResult->expressionResultPrivate.writer);

	switch (bsonValue.value_type)
	{
		case BSON_TYPE_EOD:
		case BSON_TYPE_BOOL:
		case BSON_TYPE_DATE_TIME:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_NULL:
		case BSON_TYPE_OID:
		case BSON_TYPE_TIMESTAMP:
		{
			/* These types don't need additional storage */
			expressionResult->value = bsonValue;
			break;
		}

		default:
		{
			/*
			 * Types such as string, binary, object, array all point to the
			 * data in the underlying storage of the writer. In this case,
			 * we have to create a copy that is owned by the result and will be
			 * guaranteed to live for the lifetime of the expression
			 */
			pgbson *pgbson = BsonValueToDocumentPgbson(&bsonValue);
			expressionResult->expressionResultPrivate.tracker->itemsToFree =
				lappend(expressionResult->expressionResultPrivate.tracker->itemsToFree,
						pgbson);
			pgbsonelement element;
			PgbsonToSinglePgbsonElement(pgbson, &element);
			expressionResult->value = element.bsonValue;
			break;
		}
	}

	expressionResult->expressionResultPrivate.valueSet = true;
	if (expressionResult->expressionResultPrivate.hasBaseWriter)
	{
		PgbsonWriterFree(&expressionResult->expressionResultPrivate.baseWriter);
		expressionResult->expressionResultPrivate.hasBaseWriter = false;
		expressionResult->isExpressionWriter = false;
	}

	if (!expressionResult->expressionResultPrivate.variableContext.hasSingleVariable)
	{
		hash_destroy(
			expressionResult->expressionResultPrivate.variableContext.context.table);
		expressionResult->expressionResultPrivate.variableContext.context.table = NULL;
	}
}


/*
 * Creates a child expressionResult that is tied to the parent
 * in terms of its lifetime (nested temporary objects created).
 */
ExpressionResult
ExpressionResultCreateChild(ExpressionResult *parent)
{
	ExpressionResult childExpression = ExpressionResultCreateWithTracker(
		parent->expressionResultPrivate.tracker);
	childExpression.expressionResultPrivate.variableContext.parent =
		&parent->expressionResultPrivate.variableContext;

	return childExpression;
}


/*
 * Resets the state of the expression result so that it can be reused.
 * This method is only safe to use in the following scenarios.
 * The ExpressionResult was created with ExpressionResultCreateChild and one of the following:
 * It has been evaluated with EvaluteExpression, or
 * either ExpressionResultSetValue or ExpressionResultSetValueFromWriter has been called.
 */
void
ExpressionResultReset(ExpressionResult *expressionResult)
{
	if (expressionResult->isExpressionWriter)
	{
		ereport(ERROR, (errmsg(
							"Cannot reset an expression result that was created from a writer.")));
	}

	expressionResult->expressionResultPrivate.valueSet = false;
	memset(&expressionResult->value, 0, sizeof(bson_value_t));

	if (expressionResult->expressionResultPrivate.hasBaseWriter)
	{
		PgbsonWriterFree(&expressionResult->expressionResultPrivate.baseWriter);
		expressionResult->expressionResultPrivate.hasBaseWriter = false;
	}
}


/*
 * Gets a writer used to build the result of an expression.
 * This is primarily needed in the case of field expressions
 * (e.g. $a.b) where it's a multi-valued result and requires
 * a writer to walk the tree and build the result.
 */
pgbson_element_writer *
ExpressionResultGetElementWriter(ExpressionResult *context)
{
	if (context->expressionResultPrivate.valueSet)
	{
		ereport(ERROR, (errmsg("Cannot set the result when it has been set already")));
	}
	if (context->isExpressionWriter)
	{
		/* Result was pre-initialized with a writer */
		/* return it. */
		return &context->expressionResultPrivate.writer;
	}
	else
	{
		/* initialize a new writer and use it to write to the field "". */
		context->expressionResultPrivate.hasBaseWriter = true;
		PgbsonWriterInit(&context->expressionResultPrivate.baseWriter);
		PgbsonInitObjectElementWriter(&context->expressionResultPrivate.baseWriter,
									  &context->expressionResultPrivate.writer, "", 0);
		context->isExpressionWriter = true;
		return &context->expressionResultPrivate.writer;
	}
}


/* Handles an operator expression that takes  number of arguments in some range from min to max,
 * and parses the provided arguments and calls the hook functions provided in the context
 * in order to calculate the result depending on the operator. */
void
HandleRangedArgumentExpression(pgbson *doc,
							   const bson_value_t *operatorValue,
							   ExpressionResult *expressionResult,
							   int minRequiredArgs,
							   int maxRequiredArgs,
							   const char *operatorName,
							   ExpressionArgumentHandlingContext *context)
{
	Assert(maxRequiredArgs > minRequiredArgs);

	bson_value_t result;
	bool isNullOnEmpty = false;

	if (operatorValue->value_type != BSON_TYPE_ARRAY)
	{
		if (minRequiredArgs != 1)
		{
			ThrowExpressionNumOfArgsOutsideRange(operatorName, minRequiredArgs,
												 maxRequiredArgs, 1);
		}

		ExpressionResult childExpressionResult = ExpressionResultCreateChild(
			expressionResult);
		EvaluateExpression(doc, operatorValue, &childExpressionResult, isNullOnEmpty);
		context->processElementFunc(&result,
									&childExpressionResult.value,
									childExpressionResult.isFieldPathExpression,
									context->state);
	}
	else
	{
		int inputArrayLength = BsonDocumentValueCountKeys(operatorValue);

		if (inputArrayLength < minRequiredArgs || inputArrayLength > maxRequiredArgs)
		{
			ThrowExpressionNumOfArgsOutsideRange(operatorName, minRequiredArgs,
												 maxRequiredArgs, inputArrayLength);
		}

		bson_iter_t arrayIterator;
		BsonValueInitIterator(operatorValue, &arrayIterator);

		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *expressionValue = bson_iter_value(&arrayIterator);

			ExpressionResult childExpressionResult = ExpressionResultCreateChild(
				expressionResult);
			EvaluateExpression(doc, expressionValue, &childExpressionResult,
							   isNullOnEmpty);

			/* isFieldPathExpression is passed down in case it is needed for validation
			 * as native mongo emits different error messages if the expression is
			 * a field expression (expression writer) or a value expression. */

			context->processElementFunc(&result,
										&childExpressionResult.value,
										childExpressionResult.isFieldPathExpression,
										context->state);
		}
	}

	if (context->processExpressionResultFunc != NULL)
	{
		context->processExpressionResultFunc(&result, context->state);
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/* Handles an operator expression that takes any number of arguments,
 * and parses the provided arguments and calls the hook functions provided in the context
 * in order to calculate the result depending on the operator. */
void
HandleVariableArgumentExpression(pgbson *doc, const bson_value_t *operatorValue,
								 ExpressionResult *expressionResult,
								 bson_value_t *startValue,
								 ExpressionArgumentHandlingContext *context)
{
	bson_value_t *result = startValue;
	bool isNullOnEmpty = false;

	if (operatorValue->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIterator;
		BsonValueInitIterator(operatorValue, &arrayIterator);

		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *expressionValue = bson_iter_value(&arrayIterator);

			ExpressionResult childExpressionResult = ExpressionResultCreateChild(
				expressionResult);
			EvaluateExpression(doc, expressionValue, &childExpressionResult,
							   isNullOnEmpty);

			bson_value_t evaluatedValue = childExpressionResult.value;

			/* isFieldPathExpression is passed down in case it is needed for validation
			 * as native mongo emits different error messages if the expression is
			 * a field expression (expression writer) or a value expression. */
			bool continueEnumerating =
				context->processElementFunc(result,
											&evaluatedValue,
											childExpressionResult.isFieldPathExpression,
											context->state);

			if (!continueEnumerating)
			{
				ExpressionResultSetValue(expressionResult, result);
				return;
			}
		}
	}
	else
	{
		ExpressionResult childExpressionResult = ExpressionResultCreateChild(
			expressionResult);
		EvaluateExpression(doc, operatorValue, &childExpressionResult, isNullOnEmpty);

		context->processElementFunc(result, &childExpressionResult.value,
									childExpressionResult.isFieldPathExpression,
									context->state);
	}

	if (context->processExpressionResultFunc != NULL)
	{
		context->processExpressionResultFunc(result, context->state);
	}

	ExpressionResultSetValue(expressionResult, result);
}


/* Handles an operator expression that takes a fixed number of arguments,
 * and validates that the number provided is exactly the expected one.
 * It calls the hook functions provided in the context in order to process arguments
 * and to calculate the result depending on the operator. */
void
HandleFixedArgumentExpression(pgbson *doc, const bson_value_t *operatorValue,
							  ExpressionResult *expressionResult,
							  int numberOfExpectedArgs,
							  const char *operatorName,
							  ExpressionArgumentHandlingContext *context)
{
	Assert(numberOfExpectedArgs > 0);
	bool isNullOnEmpty = false;

	bson_value_t result;
	if (operatorValue->value_type != BSON_TYPE_ARRAY)
	{
		if (numberOfExpectedArgs > 1)
		{
			if (context->throwErrorInvalidNumberOfArgsFunc != NULL)
			{
				context->throwErrorInvalidNumberOfArgsFunc(operatorName,
														   numberOfExpectedArgs, 1);
				Assert(false); /* Should never be hit as the above function should always throw. */
			}
			else
			{
				ThrowExpressionTakesExactlyNArgs(operatorName,
												 numberOfExpectedArgs, 1);
			}
		}

		ExpressionResult childExpressionResult = ExpressionResultCreateChild(
			expressionResult);
		EvaluateExpression(doc, operatorValue, &childExpressionResult, isNullOnEmpty);

		/* Here we will eval the expression and setup for the operator to be called,
		 * Note that any processing of special terms like $$ROOT etc have been already
		 * processed.
		 * This the "legacy operator" processing flow.
		 * We should migrate all operators to the newer style of (1) Parse,
		 * and then later (2) Eval, where we try to do re-writing of expressions and
		 * whatever process once before we do the Eval for each document.
		 * This next call is the legacy operators processor */
		context->processElementFunc(&result, &childExpressionResult.value,
									childExpressionResult.isFieldPathExpression,
									context->state);
	}
	else
	{
		int numArgs = 0;
		bson_iter_t arrayIterator;

		/* In order to match native mongo, we need to do this in 2 passes.
		 * First pass to validate number of args is correct.
		 * Second pass evaluate the expressions.
		 * We need to do it in 2 different passes as if we evaluate
		 * expressions in the first pass and a nested expression throws an error
		 * we would report that error, rather than the wrong number of args error,
		 * and native mongo's wrong number of args error always wins. */
		numArgs = BsonDocumentValueCountKeys(operatorValue);

		if (numArgs != numberOfExpectedArgs)
		{
			if (context->throwErrorInvalidNumberOfArgsFunc != NULL)
			{
				context->throwErrorInvalidNumberOfArgsFunc(operatorName,
														   numberOfExpectedArgs, numArgs);
				Assert(false); /* Should never be hit as the above function should always throw. */
			}
			else
			{
				ThrowExpressionTakesExactlyNArgs(operatorName,
												 numberOfExpectedArgs, numArgs);
			}
		}

		BsonValueInitIterator(operatorValue, &arrayIterator);

		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *value = bson_iter_value(&arrayIterator);
			ExpressionResult childExpressionResult = ExpressionResultCreateChild(
				expressionResult);
			EvaluateExpression(doc, value, &childExpressionResult, isNullOnEmpty);

			context->processElementFunc(&result, &childExpressionResult.value,
										childExpressionResult.isFieldPathExpression,
										context->state);
		}
	}

	if (context->processExpressionResultFunc != NULL)
	{
		context->processExpressionResultFunc(&result, context->state);
	}

	/* If an operator doesn't provide a result should set the type to EOD, i.e: $arrayElemAt
	 * An operator could set the result via a writer, so if it is set, don't override it. */
	if (result.value_type != BSON_TYPE_EOD &&
		!expressionResult->expressionResultPrivate.valueSet)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


/* Hook function in order to process arguments for operators that take exactly two arguments.
 * Both operands are stored in the state.
 * It also sets if any operand is null or undefined and if any operand is a field expression.
 * This follows a function pointer contract, hence we need to return a bool. */
bool
ProcessDualArgumentElement(bson_value_t *result,
						   const bson_value_t *currentElement,
						   bool isFieldPathExpression, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	bson_value_t element = *currentElement;

	if (!dualState->isFirstProcessed)
	{
		dualState->firstArgument = element;
		dualState->isFirstProcessed = true;
	}
	else
	{
		dualState->secondArgument = element;
	}

	dualState->hasFieldExpression = dualState->hasFieldExpression ||
									isFieldPathExpression;

	dualState->hasNullOrUndefined = dualState->hasNullOrUndefined ||
									IsExpressionResultNullOrUndefined(currentElement);
	return true;
}


/* Process elements for operators which take at most three arguments eg, $slice, $range */
bool
ProcessThreeArgumentElement(bson_value_t *result, const
							bson_value_t *currentElement,
							bool isFieldPathExpression, void *state)
{
	ThreeArgumentExpressionState *threeArgState = (ThreeArgumentExpressionState *) state;

	if (threeArgState->totalProcessedArgs == 0)
	{
		threeArgState->firstArgument = *currentElement;
	}
	else if (threeArgState->totalProcessedArgs == 1)
	{
		threeArgState->secondArgument = *currentElement;
	}
	else if (threeArgState->totalProcessedArgs == 2)
	{
		threeArgState->thirdArgument = *currentElement;
	}
	else
	{
		elog(ERROR,
			 "The ProcessThreeArgumentElement function requires a minimum of 1 argument and a maximum of 3 arguments, but you have passed %d arguments.",
			 threeArgState->totalProcessedArgs + 1);
	}

	threeArgState->hasNullOrUndefined = threeArgState->hasNullOrUndefined ||
										IsExpressionResultNullOrUndefined(currentElement);
	threeArgState->totalProcessedArgs++;
	return true;
}


/* Process elements for operators which take at most four arguments eg, $indexOfBytes, $indexOfCP */
bool
ProcessFourArgumentElement(bson_value_t *result, const
						   bson_value_t *currentElement,
						   bool isFieldPathExpression, void *state)
{
	FourArgumentExpressionState *fourArgState = (FourArgumentExpressionState *) state;

	if (fourArgState->totalProcessedArgs == 0)
	{
		fourArgState->firstArgument = *currentElement;
	}
	else if (fourArgState->totalProcessedArgs == 1)
	{
		fourArgState->secondArgument = *currentElement;
	}
	else if (fourArgState->totalProcessedArgs == 2)
	{
		fourArgState->thirdArgument = *currentElement;
	}
	else if (fourArgState->totalProcessedArgs == 3)
	{
		fourArgState->fourthArgument = *currentElement;
	}
	else
	{
		elog(ERROR,
			 "The ProcessFourArgumentElement function requires a minimum of 1 argument and a maximum of 4 arguments, but you have passed %d arguments.",
			 fourArgState->totalProcessedArgs + 1);
	}
	fourArgState->hasNullOrUndefined = fourArgState->hasNullOrUndefined ||
									   IsExpressionResultNullOrUndefined(currentElement);
	fourArgState->totalProcessedArgs++;
	return true;
}


/* --------------------------------------------------------- */
/* New Aggregation Operator Parsing Framework functions      */
/* --------------------------------------------------------- */

/* Function that parses a given expression in the bson_value_t and populates the
 * expressionData with the expression information. */
void
ParseAggregationExpressionData(AggregationExpressionData *expressionData,
							   const bson_value_t *value, const
							   ExpressionVariableContext *variableContext)
{
	/* Specific operators' parse functions will call into this function recursively to parse its arguments. */
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		ParseDocumentAggregationExpressionData(value, expressionData, variableContext);
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		ParseArrayAggregationExpressionData(value, expressionData, variableContext);
	}
	else
	{
		if (value->value_type == BSON_TYPE_UTF8 && value->value.v_utf8.len > 1 &&
			value->value.v_utf8.str[0] == '$')
		{
			if (value->value.v_utf8.len > 2 && value->value.v_utf8.str[1] == '$')
			{
				StringView expressionView = {
					.string = value->value.v_utf8.str, .length = value->value.v_utf8.len
				};
				StringView dottedSuffix = StringViewFindSuffix(&expressionView, '.');
				if (dottedSuffix.length > 0)
				{
					expressionView = StringViewFindPrefix(&expressionView, '.');
				}

				if (StringViewEqualsCString(&expressionView, "$$NOW"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Now;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$CLUSTER_TIME"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_ClusterTime;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$ROOT"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Root;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$CURRENT"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Current;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$REMOVE"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Remove;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$DESCEND"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Descend;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$PRUNE"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Prune;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$KEEP"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_Keep;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$SEARCH_META"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_SearchMeta;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else if (StringViewEqualsCString(&expressionView, "$$USER_ROLES"))
				{
					expressionData->kind = AggregationExpressionKind_SystemVariable;
					expressionData->systemVariable.kind =
						AggregationExpressionSystemVariableKind_UserRoles;
					expressionData->systemVariable.pathSuffix = dottedSuffix;
				}
				else
				{
					expressionData->kind = AggregationExpressionKind_Variable;
					expressionData->value = *value;
				}
			}
			else
			{
				expressionData->kind = AggregationExpressionKind_Path;
				expressionData->value = *value;

				if (strlen(value->value.v_utf8.str) != value->value.v_utf8.len)
				{
					ereport(ERROR, (errcode(MongoLocation16411),
									errmsg(
										"FieldPath field names may not contain embedded nulls")));
				}
			}
		}
		else
		{
			expressionData->kind = AggregationExpressionKind_Constant;
			expressionData->value = *value;
		}
	}

	Assert(expressionData->kind != AggregationExpressionKind_Invalid);
}


/* Evaluates the aggregation expression data for a preparsed aggregation operator,
 * and sets the value into the given expression result. */
void
EvaluateAggregationExpressionData(const AggregationExpressionData *expressionData,
								  pgbson *document, ExpressionResult *expressionResult,
								  bool isNullOnEmpty)
{
	switch (expressionData->kind)
	{
		case AggregationExpressionKind_Operator:
		{
			/* If the operator already supports the preparsed framework, use that to evaluate it.*/
			if (expressionData->operator.handleExpressionFunc != NULL)
			{
				expressionData->operator.handleExpressionFunc(document,
															  expressionData->operator.
															  arguments,
															  expressionResult);
			}
			else
			{
				expressionData->operator.legacyEvaluateOperatorFunc(document,
																	&expressionData->
																	operator.
																	expressionValue,
																	expressionResult);
			}

			break;
		}

		case AggregationExpressionKind_Document:
		{
			pgbson_element_writer *elementWriter =
				ExpressionResultGetElementWriter(expressionResult);
			EvaluateAggregationExpressionDocumentToWriter(expressionData, document,
														  elementWriter,
														  &expressionResult->
														  expressionResultPrivate.
														  variableContext,
														  isNullOnEmpty);
			ExpressionResultSetValueFromWriter(expressionResult);
			break;
		}

		case AggregationExpressionKind_Array:
		{
			pgbson_array_writer childArrayWriter;
			pgbson_element_writer *elementWriter =
				ExpressionResultGetElementWriter(expressionResult);
			PgbsonElementWriterStartArray(elementWriter, &childArrayWriter);
			EvaluateAggregationExpressionArrayToWriter(expressionData, document,
													   &childArrayWriter,
													   &expressionResult->
													   expressionResultPrivate.
													   variableContext);
			PgbsonElementWriterEndArray(elementWriter, &childArrayWriter);
			ExpressionResultSetValueFromWriter(expressionResult);
			break;
		}

		case AggregationExpressionKind_Constant:
		{
			ExpressionResultSetValue(expressionResult, &expressionData->value);
			break;
		}

		case AggregationExpressionKind_Variable:
		{
			EvaluateAggregationExpressionVariable(expressionData, document,
												  expressionResult,
												  isNullOnEmpty);
			break;
		}

		case AggregationExpressionKind_SystemVariable:
		{
			EvaluateAggregationExpressionSystemVariable(expressionData, document,
														expressionResult,
														isNullOnEmpty);
			break;
		}

		case AggregationExpressionKind_Path:
		{
			/* path expressions are equivalent to $$CURRENT.<path> */
			AggregationExpressionData currentExpressionData;
			currentExpressionData.kind = AggregationExpressionKind_SystemVariable;
			currentExpressionData.systemVariable.kind =
				AggregationExpressionSystemVariableKind_Current;
			currentExpressionData.systemVariable.pathSuffix.string =
				expressionData->value.value.v_utf8.str + 1;
			currentExpressionData.systemVariable.pathSuffix.length =
				expressionData->value.value.v_utf8.len - 1;

			EvaluateAggregationExpressionSystemVariable(&currentExpressionData,
														document,
														expressionResult,
														isNullOnEmpty);
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unexpected aggregation expression kind %d",
								   expressionData->kind)));
		}
	}
}


/* Evaluates the aggregation expression data for a preparsed aggregation operator into the specified writer. */
void
EvaluateAggregationExpressionDataToWriter(const AggregationExpressionData *expressionData,
										  pgbson *document, StringView path,
										  pgbson_writer *writer,
										  const ExpressionVariableContext *variableContext,
										  bool
										  isNullOnEmpty)
{
	switch (expressionData->kind)
	{
		case AggregationExpressionKind_Operator:
		{
			ExpressionLifetimeTracker tracker = { 0 };
			pgbson_element_writer elementWriter;
			PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
										  path.length);
			ExpressionResult expressionResult = ExpressionResultCreateFromElementWriter(
				&elementWriter, &tracker, variableContext);

			/* If the operator already implements the preparsed framework, use that data to evaluate it. */
			if (expressionData->operator.handleExpressionFunc != NULL)
			{
				expressionData->operator.handleExpressionFunc(document,
															  expressionData->operator.
															  arguments,
															  &expressionResult);
			}
			else
			{
				expressionData->operator.legacyEvaluateOperatorFunc(document,
																	&expressionData->
																	operator.
																	expressionValue,
																	&expressionResult);
			}

			list_free_deep(tracker.itemsToFree);
			break;
		}

		case AggregationExpressionKind_Array:
		{
			pgbson_array_writer arrayWriter;
			PgbsonWriterStartArray(writer, path.string, path.length, &arrayWriter);
			EvaluateAggregationExpressionArrayToWriter(expressionData, document,
													   &arrayWriter, variableContext);
			PgbsonWriterEndArray(writer, &arrayWriter);
			break;
		}

		case AggregationExpressionKind_Document:
		{
			pgbson_element_writer elementWriter;
			PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
										  path.length);
			EvaluateAggregationExpressionDocumentToWriter(expressionData, document,
														  &elementWriter, variableContext,
														  isNullOnEmpty);
			break;
		}

		case AggregationExpressionKind_Constant:
		{
			pgbson_element_writer elementWriter;
			PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
										  path.length);
			PgbsonElementWriterWriteValue(&elementWriter, &expressionData->value);
			break;
		}

		case AggregationExpressionKind_Variable:
		{
			ExpressionLifetimeTracker tracker = { 0 };
			pgbson_element_writer elementWriter;
			PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
										  path.length);
			ExpressionResult expressionResult = ExpressionResultCreateFromElementWriter(
				&elementWriter, &tracker, variableContext);
			EvaluateAggregationExpressionVariable(expressionData, document,
												  &expressionResult,
												  isNullOnEmpty);
			list_free_deep(tracker.itemsToFree);
			break;
		}

		case AggregationExpressionKind_SystemVariable:
		{
			ExpressionLifetimeTracker tracker = { 0 };
			pgbson_element_writer elementWriter;
			PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
										  path.length);
			ExpressionResult expressionResult = ExpressionResultCreateFromElementWriter(
				&elementWriter, &tracker, variableContext);
			EvaluateAggregationExpressionSystemVariable(expressionData, document,
														&expressionResult,
														isNullOnEmpty);
			list_free_deep(tracker.itemsToFree);
			break;
		}

		case AggregationExpressionKind_Path:
		{
			/* path expressions are equivalent to $$CURRENT.<path> */
			AggregationExpressionData currentExpressionData;
			currentExpressionData.kind = AggregationExpressionKind_SystemVariable;
			currentExpressionData.systemVariable.kind =
				AggregationExpressionSystemVariableKind_Current;
			currentExpressionData.systemVariable.pathSuffix.string =
				expressionData->value.value.v_utf8.str + 1;
			currentExpressionData.systemVariable.pathSuffix.length =
				expressionData->value.value.v_utf8.len - 1;

			ExpressionLifetimeTracker tracker = { 0 };
			pgbson_element_writer elementWriter;
			PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
										  path.length);
			ExpressionResult expressionResult = ExpressionResultCreateFromElementWriter(
				&elementWriter, &tracker, variableContext);
			EvaluateAggregationExpressionSystemVariable(&currentExpressionData,
														document,
														&expressionResult,
														isNullOnEmpty);
			list_free_deep(tracker.itemsToFree);
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unexpected aggregation expression kind %d",
								   expressionData->kind)));
		}
	}
}


/* Parses the expressions that are specified as arguments for an operator and returns
 * the list of AggregationExpressionData arguments or a single AggregationExpressionData for single arg expressions. */
void *
ParseFixedArgumentsForExpression(const bson_value_t *argumentValue, int
								 numberOfExpectedArgs, const char *operatorName,
								 AggregationExpressionArgumentsKind *argumentsKind,
								 const ExpressionVariableContext *variableContext)
{
	Assert(numberOfExpectedArgs > 0);

	if (argumentValue->value_type != BSON_TYPE_ARRAY)
	{
		if (numberOfExpectedArgs > 1)
		{
			ThrowExpressionTakesExactlyNArgs(operatorName, numberOfExpectedArgs, 1);
		}

		AggregationExpressionData *argumentData = palloc0(
			sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(argumentData, argumentValue, variableContext);
		*argumentsKind = AggregationExpressionArgumentsKind_Palloc;
		return argumentData;
	}

	int numArgs = BsonDocumentValueCountKeys(argumentValue);

	/* In order to match native mongo, we need to do this in 2 passes.
	 * First pass to validate number of args is correct.
	 * Second pass evaluate the expressions.
	 * We need to do it in 2 different passes as if we evaluate
	 * expressions in the first pass and a nested expression throws an error
	 * we would report that error, rather than the wrong number of args error,
	 * and native mongo's wrong number of args error always wins. */
	if (numArgs != numberOfExpectedArgs)
	{
		ThrowExpressionTakesExactlyNArgs(operatorName, numberOfExpectedArgs, numArgs);
	}

	bson_iter_t arrayIterator;
	BsonValueInitIterator(argumentValue, &arrayIterator);

	/*
	 * When operator expects a single argument and input is an array of single element,
	 * return the element as argument (not as a list) to match the scenario when input is not an array.
	 */
	if (numArgs == 1)
	{
		bson_iter_next(&arrayIterator);
		const bson_value_t *arg = bson_iter_value(&arrayIterator);
		AggregationExpressionData *argumentData = palloc0(
			sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(argumentData, arg, variableContext);
		*argumentsKind = AggregationExpressionArgumentsKind_Palloc;
		return argumentData;
	}
	else
	{
		List *arguments = NIL;
		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *arg = bson_iter_value(&arrayIterator);
			AggregationExpressionData *argData = palloc0(
				sizeof(AggregationExpressionData));
			ParseAggregationExpressionData(argData, arg, variableContext);
			arguments = lappend(arguments, argData);
		}

		*argumentsKind = AggregationExpressionArgumentsKind_List;
		return arguments;
	}
}


/* Evaluates a preparsed aggregation expression data of type Variable into the specified writer,
 * using the provided variable context. */
static void
EvaluateAggregationExpressionVariable(const AggregationExpressionData *data,
									  pgbson *document,
									  ExpressionResult *expressionResult,
									  bool isNullOnEmpty)
{
	Assert(data->kind == AggregationExpressionKind_Variable);

	/* Variable expressions are prefixed with $$ so skip the prefix to search the variable. */
	StringView dottedExpression = {
		.length = data->value.value.v_utf8.len - 2,
		.string = data->value.value.v_utf8.str + 2,
	};

	bool isDottedExpression = StringViewContains(&dottedExpression, '.');
	StringView varName = dottedExpression;

	if (isDottedExpression)
	{
		varName = StringViewFindPrefix(&dottedExpression, '.');
	}

	bson_value_t variableValue;
	if (!ExpressionResultGetVariable(varName, expressionResult, document, &variableValue))
	{
		ereport(ERROR, (errcode(MongoLocation17276),
						errmsg("Use of undefined variable: %s",
							   CreateStringFromStringView(&varName))));
	}

	expressionResult->isFieldPathExpression = isDottedExpression;
	if (!isDottedExpression)
	{
		ExpressionResultSetValue(expressionResult, &variableValue);
		return;
	}

	/* Evaluate dotted expression for a variable */
	StringView varNameDottedSuffix = StringViewFindSuffix(
		&dottedExpression, '.');
	EvaluateFieldPathAndWrite(&variableValue, varNameDottedSuffix.string,
							  varNameDottedSuffix.length,
							  ExpressionResultGetElementWriter(expressionResult),
							  isNullOnEmpty);
	ExpressionResultSetValueFromWriter(expressionResult);
}


/* Evaluates a preparsed aggregation expression data of type SystemVariable into the specified expression result,
 * using its variable context. */
static void
EvaluateAggregationExpressionSystemVariable(const AggregationExpressionData *data,
											pgbson *document,
											ExpressionResult *expressionResult,
											bool isNullOnEmpty)
{
	Assert(data->kind == AggregationExpressionKind_SystemVariable);

	bson_value_t variableValue = { 0 };
	switch (data->systemVariable.kind)
	{
		case AggregationExpressionSystemVariableKind_Root:
		{
			variableValue = ConvertPgbsonToBsonValue(document);
			break;
		}

		case AggregationExpressionSystemVariableKind_Now:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$NOW not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_ClusterTime:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$CLUSTER_TIME not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_Current:
		{
			if (!ExpressionResultGetVariable(CurrentVariableName, expressionResult,
											 document,
											 &variableValue))
			{
				/*
				 * Once expressions are moved to the new framework ExpressionResultGetVariable
				 * won't handle $$CURRENT. $$CURRENT == $$ROOT default will get handled here.
				 */
				variableValue = ConvertPgbsonToBsonValue(document);
			}

			break;
		}

		case AggregationExpressionSystemVariableKind_Remove:
		{
			return;
		}

		case AggregationExpressionSystemVariableKind_Descend:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$DESCEND not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_Prune:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$PRUNE not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_Keep:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$KEEP not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_SearchMeta:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$SEARCH_META not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_UserRoles:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Variable $$USER_ROLES not supported yet")));
		}

		default:
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg(
								"Unknown system variable encountered. This is a bug")));
		}
	}

	expressionResult->isFieldPathExpression = false;

	if (data->systemVariable.pathSuffix.length == 0)
	{
		ExpressionResultSetValue(expressionResult, &variableValue);
		return;
	}

	expressionResult->isFieldPathExpression = true;
	EvaluateFieldPathAndWrite(&variableValue, data->systemVariable.pathSuffix.string,
							  data->systemVariable.pathSuffix.length,
							  ExpressionResultGetElementWriter(expressionResult),
							  isNullOnEmpty);
	ExpressionResultSetValueFromWriter(expressionResult);
}


/* Evaluates a preparsed aggregation expression of type Document into the specified writer. */
static void
EvaluateAggregationExpressionDocumentToWriter(const AggregationExpressionData *data,
											  pgbson *document,
											  pgbson_element_writer *writer,
											  const ExpressionVariableContext *
											  variableContext,
											  bool isNullOnEmpty)
{
	Assert(data->kind == AggregationExpressionKind_Document);

	WriteTreeContext context =
	{
		.state = NULL,
		.filterNodeFunc = NULL,
		.isNullOnEmpty = isNullOnEmpty,
	};

	pgbson_writer childWriter;
	PgbsonElementWriterStartDocument(writer, &childWriter);
	TraverseTreeAndWriteFieldsToWriter(data->expressionTree, &childWriter,
									   document,
									   &context,
									   variableContext);
	PgbsonElementWriterEndDocument(writer, &childWriter);
}


/* Evaluates a preparsed aggregation expression data of type Array into the specified writer. */
static void
EvaluateAggregationExpressionArrayToWriter(const AggregationExpressionData *data,
										   pgbson *document,
										   pgbson_array_writer *arrayWriter,
										   const ExpressionVariableContext *
										   variableContext)
{
	Assert(data->kind == AggregationExpressionKind_Array);
	Assert(data->expressionTree->childData.numChildren == 1);

	const BsonPathNode *node;
	foreach_child(node, data->expressionTree)
	{
		const BsonLeafArrayWithFieldPathNode *leafArrayNode = CastAsLeafArrayFieldNode(
			node);
		AppendLeafArrayFieldChildrenToWriter(arrayWriter, leafArrayNode, document,
											 variableContext);
	}
}


/* Parses an expression specified as a bson_value_t document into the specified expressionData. */
static void
ParseDocumentAggregationExpressionData(const bson_value_t *value,
									   AggregationExpressionData *expressionData,
									   const ExpressionVariableContext *variableContext)
{
	Assert(value->value_type == BSON_TYPE_DOCUMENT);

	bson_iter_t docIter;
	BsonValueInitIterator(value, &docIter);
	if (bson_iter_next(&docIter) && bson_iter_key_len(&docIter) > 1 && bson_iter_key(
			&docIter)[0] == '$')
	{
		const char *operatorKey = bson_iter_key(&docIter);
		const bson_value_t *argument = bson_iter_value(&docIter);

		if (bson_iter_next(&docIter))
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
								"an expression operator specification must contain exactly one field, found 2 fields '%s' and '%s'.",
								operatorKey,
								bson_iter_key(&docIter))));
		}

		expressionData->kind = AggregationExpressionKind_Operator;
		MongoOperatorExpression searchKey = {
			.operatorName = operatorKey,
			.legacyHandleOperatorFunc = NULL,
			.handlePreParsedOperatorFunc = NULL,
			.parseAggregationExpressionFunc = NULL,
		};

		MongoOperatorExpression *pItem = (MongoOperatorExpression *) bsearch(&searchKey,
																			 OperatorExpressions,
																			 NumberOfOperatorExpressions,
																			 sizeof(
																				 MongoOperatorExpression),
																			 CompareOperatorExpressionByName);

		if (pItem == NULL)
		{
			ereport(ERROR, (errcode(MongoLocation31325),
							errmsg("Unknown expression %s", searchKey.operatorName),
							errhint("Unknown expression %s", searchKey.operatorName)));
		}

		ReportFeatureUsage(pItem->featureCounterId);

		if (pItem->parseAggregationExpressionFunc != NULL)
		{
			pItem->parseAggregationExpressionFunc(argument, expressionData,
												  variableContext);

			if (expressionData->kind == AggregationExpressionKind_Operator)
			{
				/* The parsed expression based on its behavior an arguments was converted to a constant, don't set
				 * the handler function so that when we evaluate this expression against each document we just return the constant value. */
				expressionData->operator.handleExpressionFunc =
					pItem->handlePreParsedOperatorFunc;

				Assert(expressionData->operator.argumentsKind !=
					   AggregationExpressionArgumentsKind_Invalid);
			}
		}
		else if (pItem->legacyHandleOperatorFunc != NULL)
		{
			expressionData->operator.legacyEvaluateOperatorFunc =
				pItem->legacyHandleOperatorFunc;
			expressionData->operator.expressionValue = *argument;
		}
		else
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("Operator %s not implemented yet",
								   searchKey.operatorName),
							errhint("Operator %s not implemented yet",
									searchKey.operatorName)));
		}

		return;
	}


	/* If the document we are parsing is not an operator. i.e: {a: "1", b: { $add: [1, 1] } }
	 * we need to parse the document's nested fields in case we find nested expressions like in
	 * the previous example where b is an operator expression.
	 * We build a path tree for the document as we parse it, and if we find only constant expressions
	 * i.e {a: "1", b: "2" }, we transform the expression to a constant and store the original document value. */

	bool forceLeafExpression = true;
	bool hasFields = false;
	BsonValueInitIterator(value, &docIter);

	BuildBsonPathTreeContext context = { 0 };
	context.buildPathTreeFuncs = &DefaultPathTreeFuncs;
	context.variableContext = variableContext;
	BsonIntermediatePathNode *treeNode = BuildBsonPathTree(&docIter, &context,
														   forceLeafExpression,
														   &hasFields);

	if (context.hasAggregationExpressions)
	{
		/* the document has nested expressions, store the tree so that it is evaluated per each tuple in the query. */
		expressionData->kind = AggregationExpressionKind_Document;
		expressionData->expressionTree = treeNode;
	}
	else
	{
		ereport(DEBUG3, (errmsg("Optimizing document expression into constant.")));

		/* We write the tree instead of just copying the input value for 2 reasons:
		 * 1. Document keys need to be deduplicated to match native Mongo. Since we've built the tree already and that deduplicates keys, we use it.
		 * 2. If it is a constant document, it could've had nested operators that were transformed to a constant
		 * as the result of evaluating that operator is always constant. So we need to write the value from the parsed tree
		 * instead of just copying the input value. i.e: { "a": { $literal: "foo" } }, needs to evaluate to { "a": "foo" }. */
		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		WriteTreeContext context = {
			.filterNodeFunc = NULL,
			.isNullOnEmpty = false,
			.state = NULL
		};

		pgbson *document = PgbsonInitEmpty();
		TraverseTreeAndWriteFieldsToWriter(treeNode, &writer, document, &context, NULL);
		PgbsonWriterCopyDocumentDataToBsonValue(&writer, &expressionData->value);
		PgbsonWriterFree(&writer);
		pfree(document);

		expressionData->kind = AggregationExpressionKind_Constant;

		FreeTree(treeNode);
	}
}


/*
 * VariableHashEntryCompareFunc is the (HASHCTL.match) callback which compares the
 * two variable names to see if they match.
 *
 * Returns 0 if those two bson element keys are same, 1 otherwise.
 */
static int
VariableHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize)
{
	const VariableData *hashEntry1 = obj1;
	const VariableData *hashEntry2 = obj2;

	return CompareStringView(&hashEntry1->name, &hashEntry2->name);
}


/*
 * VariableHashEntryHashFunc is the (HASHCTL.hash) callback (based on
 * string_hash()) used to hash the entry based on the variable name.
 */
static uint32
VariableHashEntryHashFunc(const void *obj, size_t objsize)
{
	const VariableData *hashEntry = obj;
	return hash_bytes((const unsigned char *) hashEntry->name.string,
					  (int) hashEntry->name.length);
}


/* Parses an expression specified as a bson_value_t array into the specified expressionData. */
static void
ParseArrayAggregationExpressionData(const bson_value_t *value,
									AggregationExpressionData *expressionData,
									const ExpressionVariableContext *variableContext)
{
	Assert(value->value_type == BSON_TYPE_ARRAY);

	BsonIntermediatePathNode *root = MakeRootNode();

	bool isConstantArray = true;
	bool treatLeafDataAsConstant = false;
	StringView arrayPath = {
		.string = "a", .length = 1
	};
	BsonLeafArrayWithFieldPathNode *arrayNode =
		TraverseDottedPathAndAddLeafArrayNode(&arrayPath, root,
											  BsonDefaultCreateIntermediateNode,
											  treatLeafDataAsConstant, variableContext);

	/* If the expression is an array we must evaluate all the nested items in case we have
	 * a nested expression i.e: [1, 2, {$add: [1, 1]]}, "$b"]. In order to achieve this, we
	 * build a tree with the array expressions in order. However, if the array is ONLY constant
	 * expressions, we mark the kind as a constant and store the array bson_value_t. */

	int index = 0;
	const char *relativePath = NULL;
	bson_iter_t arrayIter;
	BsonValueInitIterator(value, &arrayIter);
	while (bson_iter_next(&arrayIter))
	{
		const BsonLeafPathNode *newNode = AddValueNodeToLeafArrayWithField(arrayNode,
																		   relativePath,
																		   index,
																		   bson_iter_value(
																			   &arrayIter),
																		   BsonDefaultCreateLeafNode,
																		   treatLeafDataAsConstant,
																		   variableContext);
		isConstantArray = isConstantArray &&
						  IsAggregationExpressionConstant(&newNode->fieldData);
		index++;
	}

	if (isConstantArray)
	{
		ereport(DEBUG3, (errmsg("Optimizing array expression into constant.")));

		/* If it is a constant array, it could've had nested operators that were transformed to a constant
		 * as the result of evaluating that operator is always constant. So we need to write the value from the parsed tree
		 * instead of just copying the input value. i.e: [ { $literal: "foo" } ], needs to evaluate to ["foo"]. */
		pgbson_writer writer;
		PgbsonWriterInit(&writer);
		pgbson_array_writer arrayWriter;
		PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

		/* We're writing constants, we don't have a parent document or a variable context. */
		pgbson *document = PgbsonInitEmpty();
		ExpressionVariableContext *variableContext = NULL;
		AppendLeafArrayFieldChildrenToWriter(&arrayWriter, arrayNode, document,
											 variableContext);
		PgbsonWriterEndArray(&writer, &arrayWriter);
		PgbsonArrayWriterCopyDataToBsonValue(&arrayWriter, &expressionData->value);
		PgbsonWriterFree(&writer);
		pfree(document);

		expressionData->kind = AggregationExpressionKind_Constant;

		FreeTree(root);
	}
	else
	{
		expressionData->kind = AggregationExpressionKind_Array;
		expressionData->expressionTree = root;
	}
}


void *
ParseRangeArgumentsForExpression(const bson_value_t *argumentValue,
								 int minRequiredArgs,
								 int maxRequiredArgs,
								 const char *operatorName,
								 AggregationExpressionArgumentsKind *argumentsKind,
								 const ExpressionVariableContext *variableContext)
{
	Assert(maxRequiredArgs > minRequiredArgs);
	if (argumentValue->value_type != BSON_TYPE_ARRAY)
	{
		if (minRequiredArgs != 1)
		{
			ThrowExpressionNumOfArgsOutsideRange(operatorName, minRequiredArgs,
												 maxRequiredArgs, 1);
		}

		AggregationExpressionData *argumentData = palloc0(
			sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(argumentData, argumentValue, variableContext);
		*argumentsKind = AggregationExpressionArgumentsKind_Palloc;
		return argumentData;
	}
	else
	{
		/* In order to match native mongo, we need to do this in 2 passes.
		 * First pass to validate number of args is correct.
		 * Second pass evaluate the expressions.
		 * We need to do it in 2 different passes as if we evaluate
		 * expressions in the first pass and a nested expression throws an error
		 * we would report that error, rather than the wrong number of args error,
		 * and native mongo's wrong number of args error always wins. */
		int numArgs = BsonDocumentValueCountKeys(argumentValue);

		if (numArgs < minRequiredArgs || numArgs > maxRequiredArgs)
		{
			ThrowExpressionNumOfArgsOutsideRange(operatorName, minRequiredArgs,
												 maxRequiredArgs, numArgs);
		}

		List *arguments = NIL;
		bson_iter_t arrayIterator;
		BsonValueInitIterator(argumentValue, &arrayIterator);

		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *arg = bson_iter_value(&arrayIterator);
			AggregationExpressionData *argData = palloc0(
				sizeof(AggregationExpressionData));
			ParseAggregationExpressionData(argData, arg, variableContext);
			arguments = lappend(arguments, argData);
		}

		*argumentsKind = AggregationExpressionArgumentsKind_List;
		return arguments;
	}
}


/* --------------------------------------------------------- */
/* Operator implementation functions */
/* --------------------------------------------------------- */

/*
 * Evaluates the output of a $literal expression.
 * Since a $literal is expressed as { "$literal": <const value> }
 * We simply copy the bson value expressed in the operator element into the
 * target writer.
 */
void
ParseDollarLiteral(const bson_value_t *inputDocument,
				   AggregationExpressionData *data,
				   const ExpressionVariableContext *variableContext)
{
	data->kind = AggregationExpressionKind_Constant;
	data->value = *inputDocument;
}
