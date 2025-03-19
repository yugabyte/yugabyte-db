/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression.c
 *
 * Operator expression implementations of BSON.
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
#include <access/xact.h>

#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_write.h"
#include "aggregation/bson_project.h"
#include "aggregation/bson_projection_tree.h"
#include "utils/date_utils.h"
#include "utils/feature_counter.h"
#include "utils/hashset_utils.h"
#include "utils/fmgr_utils.h"
#include "utils/version_utils.h"
#include "collation/collation.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

typedef void (*ParseAggregationExpressionFunc)(const bson_value_t *argument,
											   AggregationExpressionData *data,
											   ParseAggregationExpressionContext *context);

/*
 * The declaration of an operator used in OperatorExpressions[] below.
 * Every supported operator should register this datastructure there.
 */
typedef struct
{
	/* The mongodb name of the operator (e.g. $literal) */
	const char *operatorName;

	/* Function pointer to parse the aggregation expression. */
	ParseAggregationExpressionFunc parseAggregationExpressionFunc;

	/* Function pointer to evaluate a pre parsed expression. */
	HandlePreParsedOperatorFunc handlePreParsedOperatorFunc;

	/* The feature counter type in order to report feature usage telemetry. */
	FeatureType featureCounterId;
} MongoOperatorExpression;


/*
 * Cached state for bson_expression_get
 */
typedef struct BsonExpressionGetState
{
	AggregationExpressionData *expressionData;
	ExpressionVariableContext *variableContext;
} BsonExpressionGetState;

typedef struct BsonExpressionPartitionByFieldsGetState
{
	BsonProjectionQueryState *projectionTreeState;
} BsonExpressionPartitionByFieldsGetState;

extern bool EnableCollation;
extern bool EnableNowSystemVariable;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
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
static void EvaluateAggregationExpressionDocumentToWriter(const
														  AggregationExpressionData *data,
														  pgbson *document,
														  pgbson_element_writer *writer,
														  const ExpressionVariableContext
														  *variableContext,
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
												   ParseAggregationExpressionContext *
												   parseContext);
static void ParseArrayAggregationExpressionData(const bson_value_t *value,
												AggregationExpressionData *expressionData,
												ParseAggregationExpressionContext *
												parseContext);
static bool GetVariableValueFromData(const VariableData *variable,
									 pgbson *currentDocument,
									 ExpressionResult *parentExpressionResult,
									 const ExpressionVariableContext *variableContext,
									 bson_value_t *variableValue);
static bool ExpressionResultGetVariable(StringView variableName,
										ExpressionResult *expressionResult,
										pgbson *currentDocument,
										bson_value_t *variableValue);
static void InsertVariableToContextTable(const VariableData *variableElement,
										 HTAB *hashTable);
static bool IsOverridableSystemVariable(StringView *name);
static void VariableContextSetVariableExpression(
	ExpressionVariableContext *variableContext,
	const StringView *name,
	AggregationExpressionData *
	variableExpression);
static void ValidateVariableNameCore(StringView name, bool allowStartWithUpper);

static void ReportOperatorExpressonSyntaxError(const char *fieldA,
											   bson_iter_t *fieldBIter, bool
											   performOperatorCheck);
static int CompareOperatorExpressionByName(const void *a, const void *b);
static uint32 VariableHashEntryHashFunc(const void *obj, size_t objsize);
static int VariableHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize);

static void ParseBsonExpressionGetState(BsonExpressionGetState *getState,
										const bson_value_t *expressionValue,
										pgbson *variableSpec);
static void CreateProjectionTreeStateForPartitionByFields(
	BsonExpressionPartitionByFieldsGetState *state, pgbson *partitionBy);


/*
 *  Keep this list lexicographically sorted by the operator name,
 *  as it is binary searched on the key to find the handler.
 */
static MongoOperatorExpression OperatorExpressions[] = {
	{ "$_bucketInternal", &ParseDollarBucketInternal,
	  &HandlePreParsedDollarBucketInternal,
	  INTERNAL_FEATURE_TYPE },
	{ "$abs", &ParseDollarAbs, &HandlePreParsedDollarAbs,
	  FEATURE_AGG_OPERATOR_ABS },
	{ "$accumulator", NULL, NULL, FEATURE_AGG_OPERATOR_ACCUMULATOR },
	{ "$acos", &ParseDollarAcos, &HandlePreParsedDollarAcos,
	  FEATURE_AGG_OPERATOR_ACOS },
	{ "$acosh", &ParseDollarAcosh, &HandlePreParsedDollarAcosh,
	  FEATURE_AGG_OPERATOR_ACOSH },
	{ "$add", &ParseDollarAdd, &HandlePreParsedDollarAdd,
	  FEATURE_AGG_OPERATOR_ADD },
	{ "$addToSet", NULL, NULL, FEATURE_AGG_OPERATOR_ADDTOSET },
	{ "$allElementsTrue", &ParseDollarAllElementsTrue,
	  &HandlePreParsedDollarAllElementsTrue,
	  FEATURE_AGG_OPERATOR_ALLELEMENTSTRUE },
	{ "$and", &ParseDollarAnd, &HandlePreParsedDollarAnd,
	  FEATURE_AGG_OPERATOR_AND },
	{ "$anyElementTrue", &ParseDollarAnyElementTrue,
	  &HandlePreParsedDollarAnyElementTrue,
	  FEATURE_AGG_OPERATOR_ANYELEMENTTRUE },
	{ "$arrayElemAt", &ParseDollarArrayElemAt, &HandlePreParsedDollarArrayElemAt,
	  FEATURE_AGG_OPERATOR_ARRAYELEMAT },
	{ "$arrayToObject", &ParseDollarArrayToObject,
	  &HandlePreParsedDollarArrayToObject,
	  FEATURE_AGG_OPERATOR_ARRAYTOOBJECT },
	{ "$asin", &ParseDollarAsin, &HandlePreParsedDollarAsin,
	  FEATURE_AGG_OPERATOR_ASIN },
	{ "$asinh", &ParseDollarAsinh, &HandlePreParsedDollarAsinh,
	  FEATURE_AGG_OPERATOR_ASINH },
	{ "$atan", &ParseDollarAtan, &HandlePreParsedDollarAtan,
	  FEATURE_AGG_OPERATOR_ATAN },
	{ "$atan2", &ParseDollarAtan2, &HandlePreParsedDollarAtan2,
	  FEATURE_AGG_OPERATOR_ATAN2 },
	{ "$atanh", &ParseDollarAtanh, &HandlePreParsedDollarAtanh,
	  FEATURE_AGG_OPERATOR_ATANH },
	{ "$avg", &ParseDollarAvg, &HandlePreParsedDollarAvg,
	  FEATURE_AGG_OPERATOR_AVG },
	{ "$binarySize", &ParseDollarBinarySize, &HandlePreParsedDollarBinarySize,
	  FEATURE_AGG_OPERATOR_BINARYSIZE },
	{ "$bitAnd", &ParseDollarBitAnd, &HandlePreParsedDollarBitAnd,
	  FEATURE_AGG_OPERATOR_BITAND },
	{ "$bitNot", &ParseDollarBitNot, &HandlePreParsedDollarBitNot,
	  FEATURE_AGG_OPERATOR_BITNOT },
	{ "$bitOr", &ParseDollarBitOr, &HandlePreParsedDollarBitOr,
	  FEATURE_AGG_OPERATOR_BITOR },
	{ "$bitXor", &ParseDollarBitXor, &HandlePreParsedDollarBitXor,
	  FEATURE_AGG_OPERATOR_BITXOR },
	{ "$bsonSize", &ParseDollarBsonSize, &HandlePreParsedDollarBsonSize,
	  FEATURE_AGG_OPERATOR_BSONSIZE },
	{ "$ceil", &ParseDollarCeil, &HandlePreParsedDollarCeil,
	  FEATURE_AGG_OPERATOR_CEIL },
	{ "$cmp", &ParseDollarCmp, &HandlePreParsedDollarCmp,
	  FEATURE_AGG_OPERATOR_CMP },
	{ "$concat", &ParseDollarConcat, &HandlePreParsedDollarConcat,
	  FEATURE_AGG_OPERATOR_CONCAT },
	{ "$concatArrays", &ParseDollarConcatArrays, &HandlePreParsedDollarConcatArrays,
	  FEATURE_AGG_OPERATOR_CONCATARRAYS },
	{ "$cond", &ParseDollarCond, &HandlePreParsedDollarCond,
	  FEATURE_AGG_OPERATOR_COND },
	{ "$const", &ParseDollarLiteral, NULL, FEATURE_AGG_OPERATOR_CONST }, /* $const effectively same as $literal */
	{ "$convert", &ParseDollarConvert, &HandlePreParsedDollarConvert,
	  FEATURE_AGG_OPERATOR_CONVERT },
	{ "$cos", &ParseDollarCos, &HandlePreParsedDollarCos,
	  FEATURE_AGG_OPERATOR_COS },
	{ "$cosh", &ParseDollarCosh, &HandlePreParsedDollarCosh,
	  FEATURE_AGG_OPERATOR_COSH },
	{ "$dateAdd", &ParseDollarDateAdd, &HandlePreParsedDollarDateAdd,
	  FEATURE_AGG_OPERATOR_DATEADD },
	{ "$dateDiff", &ParseDollarDateDiff, &HandlePreParsedDollarDateDiff,
	  FEATURE_AGG_OPERATOR_DATEDIFF },
	{ "$dateFromParts", &ParseDollarDateFromParts,
	  &HandlePreParsedDollarDateFromParts, FEATURE_AGG_OPERATOR_DATEFROMPARTS },
	{ "$dateFromString", &ParseDollarDateFromString,
	  &HandlePreParsedDollarDateFromString, FEATURE_AGG_OPERATOR_DATEFROMSTRING },
	{ "$dateSubtract", &ParseDollarDateSubtract, &HandlePreParsedDollarDateSubtract,
	  FEATURE_AGG_OPERATOR_DATESUBTRACT },
	{ "$dateToParts", &ParseDollarDateToParts, &HandlePreParsedDollarDateToParts,
	  FEATURE_AGG_OPERATOR_DATETOPARTS },
	{ "$dateToString", &ParseDollarDateToString, &HandlePreParsedDollarDateToString,
	  FEATURE_AGG_OPERATOR_DATETOSTRING },
	{ "$dateTrunc", &ParseDollarDateTrunc, &HandlePreParsedDollarDateTrunc,
	  FEATURE_AGG_OPERATOR_DATETRUNC },
	{ "$dayOfMonth", &ParseDollarDayOfMonth, &HandlePreParsedDollarDayOfMonth,
	  FEATURE_AGG_OPERATOR_DAYOFMONTH },
	{ "$dayOfWeek", &ParseDollarDayOfWeek, &HandlePreParsedDollarDayOfWeek,
	  FEATURE_AGG_OPERATOR_DAYOFWEEK },
	{ "$dayOfYear", &ParseDollarDayOfYear, &HandlePreParsedDollarDayOfYear,
	  FEATURE_AGG_OPERATOR_DAYOFYEAR },
	{ "$degreesToRadians", &ParseDollarDegreesToRadians,
	  &HandlePreParsedDollarDegreesToRadians,
	  FEATURE_AGG_OPERATOR_DEGREESTORADIANS },
	{ "$divide", &ParseDollarDivide, &HandlePreParsedDollarDivide,
	  FEATURE_AGG_OPERATOR_DIVIDE },
	{ "$eq", &ParseDollarEq, &HandlePreParsedDollarEq, FEATURE_AGG_OPERATOR_EQ },
	{ "$exp", &ParseDollarExp, &HandlePreParsedDollarExp,
	  FEATURE_AGG_OPERATOR_EXP },
	{ "$filter", &ParseDollarFilter, &HandlePreParsedDollarFilter,
	  FEATURE_AGG_OPERATOR_FILTER },
	{ "$first", &ParseDollarFirst, &HandlePreParsedDollarFirst,
	  FEATURE_AGG_OPERATOR_FIRST },
	{ "$firstN", &ParseDollarFirstN, &HandlePreParsedDollarFirstN,
	  FEATURE_AGG_OPERATOR_FIRSTN },
	{ "$floor", &ParseDollarFloor, &HandlePreParsedDollarFloor,
	  FEATURE_AGG_OPERATOR_FLOOR },
	{ "$function", NULL, NULL, FEATURE_AGG_OPERATOR_FUNCTION },
	{ "$getField", ParseDollarGetField, HandlePreParsedDollarGetField,
	  FEATURE_AGG_OPERATOR_GETFIELD },
	{ "$gt", &ParseDollarGt, &HandlePreParsedDollarGt, FEATURE_AGG_OPERATOR_GT },
	{ "$gte", &ParseDollarGte, &HandlePreParsedDollarGte,
	  FEATURE_AGG_OPERATOR_GTE },
	{ "$hour", &ParseDollarHour, &HandlePreParsedDollarHour,
	  FEATURE_AGG_OPERATOR_HOUR },
	{ "$ifNull", &ParseDollarIfNull, &HandlePreParsedDollarIfNull,
	  FEATURE_AGG_OPERATOR_IFNULL },
	{ "$in", &ParseDollarIn, &HandlePreParsedDollarIn, FEATURE_AGG_OPERATOR_IN },
	{ "$indexOfArray", &ParseDollarIndexOfArray, &HandlePreParsedDollarIndexOfArray,
	  FEATURE_AGG_OPERATOR_INDEXOFARRAY },
	{ "$indexOfBytes", &ParseDollarIndexOfBytes, &HandlePreParsedDollarIndexOfBytes,
	  FEATURE_AGG_OPERATOR_INDEXOFBYTES },
	{ "$indexOfCP", &ParseDollarIndexOfCP, &HandlePreParsedDollarIndexOfCP,
	  FEATURE_AGG_OPERATOR_INDEXOFCP },
	{ "$isArray", &ParseDollarIsArray, &HandlePreParsedDollarIsArray,
	  FEATURE_AGG_OPERATOR_ISARRAY },
	{ "$isNumber", &ParseDollarIsNumber, &HandlePreParsedDollarIsNumber,
	  FEATURE_AGG_OPERATOR_ISNUMBER },
	{ "$isoDayOfWeek", &ParseDollarIsoDayOfWeek, &HandlePreParsedDollarIsoDayOfWeek,
	  FEATURE_AGG_OPERATOR_ISODAYOFWEEK },
	{ "$isoWeek", &ParseDollarIsoWeek, &HandlePreParsedDollarIsoWeek,
	  FEATURE_AGG_OPERATOR_ISOWEEK },
	{ "$isoWeekYear", &ParseDollarIsoWeekYear, &HandlePreParsedDollarIsoWeekYear,
	  FEATURE_AGG_OPERATOR_ISOWEEKYEAR },
	{ "$last", &ParseDollarLast, &HandlePreParsedDollarLast,
	  FEATURE_AGG_OPERATOR_LAST },
	{ "$lastN", &ParseDollarLastN, &HandlePreParsedDollarLastN,
	  FEATURE_AGG_OPERATOR_LASTN },
	{ "$let", &ParseDollarLet, &HandlePreParsedDollarLet,
	  FEATURE_AGG_OPERATOR_LET },
	{ "$literal", &ParseDollarLiteral, NULL, FEATURE_AGG_OPERATOR_LITERAL },
	{ "$ln", &ParseDollarLn, &HandlePreParsedDollarLn, FEATURE_AGG_OPERATOR_LN },
	{ "$log", &ParseDollarLog, &HandlePreParsedDollarLog,
	  FEATURE_AGG_OPERATOR_LOG },
	{ "$log10", &ParseDollarLog10, &HandlePreParsedDollarLog10,
	  FEATURE_AGG_OPERATOR_LOG10 },
	{ "$lt", &ParseDollarLt, &HandlePreParsedDollarLt, FEATURE_AGG_OPERATOR_LT },
	{ "$lte", &ParseDollarLte, &HandlePreParsedDollarLte,
	  FEATURE_AGG_OPERATOR_LTE },
	{ "$ltrim", &ParseDollarLtrim, &HandlePreParsedDollarLtrim,
	  FEATURE_AGG_OPERATOR_LTRIM },
	{ "$makeArray", &ParseDollarMakeArray, &HandlePreParsedDollarMakeArray,
	  FEATURE_AGG_OPERATOR_MAKE_ARRAY },
	{ "$map", &ParseDollarMap, &HandlePreParsedDollarMap,
	  FEATURE_AGG_OPERATOR_MAP },
	{ "$max", &ParseDollarMax, &HandlePreParsedDollarMax,
	  FEATURE_AGG_OPERATOR_MAX },
	{ "$maxN", &ParseDollarMaxN, &HandlePreParsedDollarMaxMinN,
	  FEATURE_AGG_OPERATOR_MAXN },
	{ "$mergeObjects", &ParseDollarMergeObjects, &HandlePreParsedDollarMergeObjects,
	  FEATURE_AGG_OPERATOR_MERGEOBJECTS },
	{ "$meta", &ParseDollarMeta, &HandlePreParsedDollarMeta,
	  FEATURE_AGG_OPERATOR_META },
	{ "$millisecond", &ParseDollarMillisecond, &HandlePreParsedDollarMillisecond,
	  FEATURE_AGG_OPERATOR_MILLISECOND },
	{ "$min", &ParseDollarMin, &HandlePreParsedDollarMin,
	  FEATURE_AGG_OPERATOR_MIN },
	{ "$minN", &ParseDollarMinN, &HandlePreParsedDollarMaxMinN,
	  FEATURE_AGG_OPERATOR_MINN },
	{ "$minute", &ParseDollarMinute, &HandlePreParsedDollarMinute,
	  FEATURE_AGG_OPERATOR_MINUTE },
	{ "$mod", &ParseDollarMod, &HandlePreParsedDollarMod,
	  FEATURE_AGG_OPERATOR_MOD },
	{ "$month", &ParseDollarMonth, &HandlePreParsedDollarMonth,
	  FEATURE_AGG_OPERATOR_MONTH },
	{ "$multiply", &ParseDollarMultiply, &HandlePreParsedDollarMultiply,
	  FEATURE_AGG_OPERATOR_MULTIPLY },
	{ "$ne", &ParseDollarNe, &HandlePreParsedDollarNe, FEATURE_AGG_OPERATOR_NE },
	{ "$not", &ParseDollarNot, &HandlePreParsedDollarNot,
	  FEATURE_AGG_OPERATOR_NOT },
	{ "$objectToArray", &ParseDollarObjectToArray,
	  &HandlePreParsedDollarObjectToArray,
	  FEATURE_AGG_OPERATOR_OBJECTTOARRAY },
	{ "$or", &ParseDollarOr, &HandlePreParsedDollarOr, FEATURE_AGG_OPERATOR_OR },
	{ "$pow", &ParseDollarPow, &HandlePreParsedDollarPow,
	  FEATURE_AGG_OPERATOR_POW },
	{ "$push", NULL, NULL, FEATURE_AGG_OPERATOR_PUSH },
	{ "$radiansToDegrees", &ParseDollarRadiansToDegrees,
	  &HandlePreParsedDollarRadiansToDegrees,
	  FEATURE_AGG_OPERATOR_RADIANSTODEGREES },
	{ "$rand", &ParseDollarRand, &HandlePreParsedDollarRand,
	  FEATURE_AGG_OPERATOR_RAND },
	{ "$range", &ParseDollarRange, &HandlePreParsedDollarRange,
	  FEATURE_AGG_OPERATOR_RANGE },
	{ "$reduce", &ParseDollarReduce, &HandlePreParsedDollarReduce,
	  FEATURE_AGG_OPERATOR_REDUCE },
	{ "$regexFind", &ParseDollarRegexFind, &HandlePreParsedDollarRegexFind,
	  FEATURE_AGG_OPERATOR_REGEXFIND },
	{ "$regexFindAll", &ParseDollarRegexFindAll, &HandlePreParsedDollarRegexFindAll,
	  FEATURE_AGG_OPERATOR_REGEXFINDALL },
	{ "$regexMatch", &ParseDollarRegexMatch, &HandlePreParsedDollarRegexMatch,
	  FEATURE_AGG_OPERATOR_REGEXMATCH },
	{ "$replaceAll", &ParseDollarReplaceAll, &HandlePreParsedDollarReplaceAll,
	  FEATURE_AGG_OPERATOR_REPLACEALL },
	{ "$replaceOne", &ParseDollarReplaceOne, &HandlePreParsedDollarReplaceOne,
	  FEATURE_AGG_OPERATOR_REPLACEONE },
	{ "$reverseArray", &ParseDollarReverseArray, HandlePreParsedDollarReverseArray,
	  FEATURE_AGG_OPERATOR_REVERSEARRAY },
	{ "$round", &ParseDollarRound, &HandlePreParsedDollarRound,
	  FEATURE_AGG_OPERATOR_ROUND },
	{ "$rtrim", &ParseDollarRtrim, &HandlePreParsedDollarRtrim,
	  FEATURE_AGG_OPERATOR_RTRIM },
	{ "$second", &ParseDollarSecond, &HandlePreParsedDollarSecond,
	  FEATURE_AGG_OPERATOR_SECOND },
	{ "$setDifference", &ParseDollarSetDifference,
	  &HandlePreParsedDollarSetDifference,
	  FEATURE_AGG_OPERATOR_SETDIFFERENCE },
	{ "$setEquals", &ParseDollarSetEquals, &HandlePreParsedDollarSetEquals,
	  FEATURE_AGG_OPERATOR_SETEQUALS },
	{ "$setField", &ParseDollarSetField, &HandlePreParsedDollarSetField,
	  FEATURE_AGG_OPERATOR_SETFIELD },
	{ "$setIntersection", &ParseDollarSetIntersection,
	  &HandlePreParsedDollarSetIntersection,
	  FEATURE_AGG_OPERATOR_SETINTERSECTION },
	{ "$setIsSubset", &ParseDollarSetIsSubset, &HandlePreParsedDollarSetIsSubset,
	  FEATURE_AGG_OPERATOR_SETISSUBSET },
	{ "$setUnion", &ParseDollarSetUnion, &HandlePreParsedDollarSetUnion,
	  FEATURE_AGG_OPERATOR_SETUNION },
	{ "$sin", &ParseDollarSin, &HandlePreParsedDollarSin,
	  FEATURE_AGG_OPERATOR_SIN },
	{ "$sinh", &ParseDollarSinh, &HandlePreParsedDollarSinh,
	  FEATURE_AGG_OPERATOR_SINH },
	{ "$size", &ParseDollarSize, &HandlePreParsedDollarSize,
	  FEATURE_AGG_OPERATOR_SIZE },
	{ "$slice", &ParseDollarSlice, &HandlePreParsedDollarSlice,
	  FEATURE_AGG_OPERATOR_SLICE },
	{ "$sortArray", &ParseDollarSortArray, &HandlePreParsedDollarSortArray,
	  FEATURE_AGG_OPERATOR_SORTARRAY },
	{ "$split", &ParseDollarSplit, &HandlePreParsedDollarSplit,
	  FEATURE_AGG_OPERATOR_SPLIT },
	{ "$sqrt", &ParseDollarSqrt, &HandlePreParsedDollarSqrt,
	  FEATURE_AGG_OPERATOR_SQRT },
	{ "$stdDevPop", NULL, NULL, FEATURE_AGG_OPERATOR_STDDEVPOP },
	{ "$stdDevSamp", NULL, NULL, FEATURE_AGG_OPERATOR_STDDEVSAMP },
	{ "$strLenBytes", &ParseDollarStrLenBytes, &HandlePreParsedDollarStrLenBytes,
	  FEATURE_AGG_OPERATOR_STRLENBYTES },
	{ "$strLenCP", &ParseDollarStrLenCP, &HandlePreParsedDollarStrLenCP,
	  FEATURE_AGG_OPERATOR_STRLENCP },
	{ "$strcasecmp", &ParseDollarStrCaseCmp, &HandlePreParsedDollarStrCaseCmp,
	  FEATURE_AGG_OPERATOR_STRCASECMP },
	{ "$substr", &ParseDollarSubstrBytes, &HandlePreParsedDollarSubstrBytes,
	  FEATURE_AGG_OPERATOR_SUBSTR }, /* MongoDB treats $substr the same as $substrBytes, including error messages */
	{ "$substrBytes", &ParseDollarSubstrBytes, &HandlePreParsedDollarSubstrBytes,
	  FEATURE_AGG_OPERATOR_SUBSTRBYTES },
	{ "$substrCP", &ParseDollarSubstrCP, &HandlePreParsedDollarSubstrCP,
	  FEATURE_AGG_OPERATOR_SUBSTRCP },
	{ "$subtract", &ParseDollarSubtract, &HandlePreParsedDollarSubtract,
	  FEATURE_AGG_OPERATOR_SUBTRACT },
	{ "$sum", &ParseDollarSum, &HandlePreParsedDollarSum,
	  FEATURE_AGG_OPERATOR_SUM },
	{ "$switch", &ParseDollarSwitch, &HandlePreParsedDollarSwitch,
	  FEATURE_AGG_OPERATOR_SWITCH },
	{ "$tan", &ParseDollarTan, &HandlePreParsedDollarTan,
	  FEATURE_AGG_OPERATOR_TAN },
	{ "$tanh", &ParseDollarTanh, &HandlePreParsedDollarTanh,
	  FEATURE_AGG_OPERATOR_TANH },
	{ "$toBool", &ParseDollarToBool, &HandlePreParsedDollarToBool,
	  FEATURE_AGG_OPERATOR_TOBOOL },
	{ "$toDate", &ParseDollarToDate, &HandlePreParsedDollarToDate,
	  FEATURE_AGG_OPERATOR_TODATE },
	{ "$toDecimal", &ParseDollarToDecimal, &HandlePreParsedDollarToDecimal,
	  FEATURE_AGG_OPERATOR_TODECIMAL },
	{ "$toDouble", &ParseDollarToDouble, &HandlePreParsedDollarToDouble,
	  FEATURE_AGG_OPERATOR_TODOUBLE },
	{ "$toHashedIndexKey", &ParseDollarToHashedIndexKey,
	  &HandlePreParsedDollarToHashedIndexKey, FEATURE_AGG_OPERATOR_TOHASHEDINDEXKEY },
	{ "$toInt", &ParseDollarToInt, &HandlePreParsedDollarToInt,
	  FEATURE_AGG_OPERATOR_TOINT },
	{ "$toLong", &ParseDollarToLong, &HandlePreParsedDollarToLong,
	  FEATURE_AGG_OPERATOR_TOLONG },
	{ "$toLower", &ParseDollarToLower, &HandlePreParsedDollarToLower,
	  FEATURE_AGG_OPERATOR_TOLOWER },
	{ "$toObjectId", &ParseDollarToObjectId, &HandlePreParsedDollarToObjectId,
	  FEATURE_AGG_OPERATOR_TOOBJECTID },
	{ "$toString", &ParseDollarToString, &HandlePreParsedDollarToString,
	  FEATURE_AGG_OPERATOR_TOSTRING },
	{ "$toUUID", &ParseDollarToUUID, &HandlePreParsedDollarToUUID,
	  FEATURE_AGG_OPERATOR_TOUUID },
	{ "$toUpper", &ParseDollarToUpper, &HandlePreParsedDollarToUpper,
	  FEATURE_AGG_OPERATOR_TOUPPER },
	{ "$trim", &ParseDollarTrim, &HandlePreParsedDollarTrim,
	  FEATURE_AGG_OPERATOR_TRIM },
	{ "$trunc", &ParseDollarTrunc, &HandlePreParsedDollarTrunc,
	  FEATURE_AGG_OPERATOR_TRUNC },
	{ "$tsIncrement", &ParseDollarTsIncrement, &HandlePreParsedDollarTsIncrement,
	  FEATURE_AGG_OPERATOR_TSINCREMENT },
	{ "$tsSecond", &ParseDollarTsSecond, &HandlePreParsedDollarTsSecond,
	  FEATURE_AGG_OPERATOR_TSSECOND },
	{ "$type", &ParseDollarType, &HandlePreParsedDollarType,
	  FEATURE_AGG_OPERATOR_TYPE },
	{ "$unsetField", &ParseDollarUnsetField, &HandlePreParsedDollarUnsetField,
	  FEATURE_AGG_OPERATOR_UNSETFIELD },
	{ "$week", &ParseDollarWeek, &HandlePreParsedDollarWeek,
	  FEATURE_AGG_OPERATOR_WEEK },
	{ "$year", &ParseDollarYear, &HandlePreParsedDollarYear,
	  FEATURE_AGG_OPERATOR_YEAR },
	{ "$zip", &ParseDollarZip, &HandlePreParsedDollarZip, FEATURE_AGG_OPERATOR_ZIP }
};

static int NumberOfOperatorExpressions = sizeof(OperatorExpressions) /
										 sizeof(MongoOperatorExpression);

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
PG_FUNCTION_INFO_V1(bson_expression_partition_get);
PG_FUNCTION_INFO_V1(bson_expression_partition_by_fields_get);
PG_FUNCTION_INFO_V1(bson_expression_map);

/*
 * bson_expression_get evaluates a bson expression from a given
 * document. This follows operator expressions as per mongo aggregation expressions.
 * The input is expected to be a bson document with a single value
 * e.g. { "sum": "$a.b"}
 * or a bson document with two values of which the second is the collation spec
 * e.g. { "sum": "$a.b", "collation": "en-u-ks-level1" }
 * The output is a bson document that contains the evaluation of the first field
 * e.g. { "sum": [ 1, 2, 3 ] }
 * and the collation spec, if any.
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

	pgbson *variableSpec = NULL;
	int argPositions[2] = { 1, 3 };
	int numArgs = 1;
	if (PG_NARGS() > 3)
	{
		variableSpec = PG_GETARG_MAYBE_NULL_PGBSON(3);
		numArgs = 2;
	}

	pgbsonelement expressionElement;
	pgbson_writer writer;

	BsonExpressionGetState expressionData;
	memset(&expressionData, 0, sizeof(BsonExpressionGetState));

	/* A collation string may be passed through to be pushed down to other functions such as bson_dollar_in for $graphLookup*/
	const char *collationString = NULL;
	bson_iter_t iter;
	if (EnableCollation && PgbsonInitIteratorAtPath(expression, "collation", &iter))
	{
		collationString = PgbsonToSinglePgbsonElementWithCollation(
			(pgbson *) expression, &expressionElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(expression, &expressionElement);
	}

	const BsonExpressionGetState *state;
	SetCachedFunctionStateMultiArgs(
		state,
		BsonExpressionGetState,
		argPositions,
		numArgs,
		ParseBsonExpressionGetState,
		&expressionElement.bsonValue,
		variableSpec);

	if (state == NULL)
	{
		ParseBsonExpressionGetState(&expressionData, &expressionElement.bsonValue,
									variableSpec);
		state = &expressionData;
	}

	StringView path = {
		.length = expressionElement.pathLength,
		.string = expressionElement.path,
	};

	PgbsonWriterInit(&writer);
	EvaluateAggregationExpressionDataToWriter(state->expressionData, document, path,
											  &writer,
											  state->variableContext, isNullOnEmpty);

	pgbson *returnedBson = PgbsonWriterGetPgbson(&writer);

	/* Add the collation, if any, to the returned bson */
	/* so it can be extracted by other functions that utilize it from bson_expression_get. */
	/* For example: the comparison filter for bson_dollar_in used in $graphLookup */
	if (IsCollationApplicable(collationString))
	{
		pgbson_writer returnedWriter;
		PgbsonWriterInit(&returnedWriter);

		bson_value_t getValue = ConvertPgbsonToBsonValue(returnedBson);
		bson_iter_t pathValueIter;
		BsonValueInitIterator(&getValue, &pathValueIter);

		while (bson_iter_next(&pathValueIter))
		{
			const bson_value_t *pathValue = bson_iter_value(&pathValueIter);
			PgbsonWriterAppendValue(&returnedWriter, bson_iter_key(&pathValueIter),
									bson_iter_key_len(&pathValueIter), pathValue);
		}

		PgbsonWriterAppendUtf8(&returnedWriter, "collation", 9,
							   collationString);

		returnedBson = PgbsonWriterGetPgbson(&returnedWriter);
	}

	PG_FREE_IF_COPY(document, 0);
	PG_RETURN_POINTER(returnedBson);
}


/*
 * `bson_expression_partition_get` evaluates the expression similar to `bson_expression_get`
 * but it throws error if the expression results are evaluated to be array.
 */
Datum
bson_expression_partition_get(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *expression = PG_GETARG_PGBSON(1);
	bool isNullOnEmpty = PG_GETARG_BOOL(2);
	pgbson *variableSpec = NULL;

	int argPositions[2] = { 1, 3 };
	int numArgs = 1;
	if (PG_NARGS() > 3)
	{
		variableSpec = PG_GETARG_MAYBE_NULL_PGBSON(3);
		numArgs = 2;
	}

	pgbsonelement expressionElement;
	pgbson_writer writer;

	BsonExpressionGetState expressionData;
	memset(&expressionData, 0, sizeof(BsonExpressionGetState));

	PgbsonToSinglePgbsonElement(expression, &expressionElement);

	const BsonExpressionGetState *state;
	SetCachedFunctionStateMultiArgs(
		state,
		BsonExpressionGetState,
		argPositions,
		numArgs,
		ParseBsonExpressionGetState,
		&expressionElement.bsonValue,
		variableSpec);

	if (state == NULL)
	{
		ParseBsonExpressionGetState(&expressionData, &expressionElement.bsonValue,
									variableSpec);
		state = &expressionData;
	}

	StringView path = {
		.length = expressionElement.pathLength,
		.string = expressionElement.path,
	};

	PgbsonWriterInit(&writer);
	EvaluateAggregationExpressionDataToWriter(state->expressionData, document, path,
											  &writer,
											  state->variableContext, isNullOnEmpty);

	pgbson *returnedBson = PgbsonWriterGetPgbson(&writer);

	pgbsonelement result;
	if (!TryGetSinglePgbsonElementFromPgbson(returnedBson, &result))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"PlanExecutor error during aggregation :: cause by :: An expression evaluated in a multi field document")));
	}

	if (result.bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"PlanExecutor error during aggregation :: caused by :: An expression used to partition "
							"cannot evaluate to value of type array")));
	}

	PG_FREE_IF_COPY(document, 0);
	PG_RETURN_POINTER(returnedBson);
}


/*
 * bson_expression_partition_by_fields_get recieves the document and the composite path fields
 * that define the unique grouping according to partitionByFields.
 */
Datum
bson_expression_partition_by_fields_get(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_MAYBE_NULL_PGBSON_PACKED(0);
	if (document == NULL)
	{
		PG_RETURN_NULL();
	}

	pgbson *partitionByFields = PG_GETARG_MAYBE_NULL_PGBSON_PACKED(1);
	if (partitionByFields == NULL)
	{
		PG_RETURN_NULL();
	}

	int argPositions[1] = { 1 };
	int numArgs = 1;

	BsonExpressionPartitionByFieldsGetState *state;
	SetCachedFunctionStateMultiArgs(
		state,
		BsonExpressionPartitionByFieldsGetState,
		argPositions,
		numArgs,
		CreateProjectionTreeStateForPartitionByFields,
		partitionByFields);

	if (state == NULL)
	{
		state = palloc0(sizeof(BsonExpressionPartitionByFieldsGetState));
		CreateProjectionTreeStateForPartitionByFields(state, partitionByFields);
	}

	pgbson *result = NULL;
	if (document != NULL && state != NULL && state->projectionTreeState != NULL)
	{
		result = ProjectDocumentWithState(document, state->projectionTreeState);
	}

	PG_FREE_IF_COPY(document, 0);
	PG_FREE_IF_COPY(partitionByFields, 1);

	if (result == NULL)
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_POINTER(result);
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
	ParseAggregationExpressionContext context = { 0 };

	SetCachedFunctionState(
		state,
		AggregationExpressionData,
		argPosition,
		ParseAggregationExpressionData,
		&expressionElement.bsonValue,
		&context);

	if (state == NULL)
	{
		memset(&context, 0, sizeof(ParseAggregationExpressionContext));
		ParseAggregationExpressionData(&expressionData, &expressionElement.bsonValue,
									   &context);
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
 * Parses the shared state for bson_expression_get
 */
static void
ParseBsonExpressionGetState(BsonExpressionGetState *getState,
							const bson_value_t *expressionValue, pgbson *variableSpec)
{
	getState->expressionData = (AggregationExpressionData *) palloc0(
		sizeof(AggregationExpressionData));
	getState->variableContext = NULL;

	ParseAggregationExpressionContext context = { 0 };

	GetTimeSystemVariablesFromVariableSpec(variableSpec,
										   &context.timeSystemVariables);

	ParseAggregationExpressionData(getState->expressionData, expressionValue, &context);

	bson_iter_t letVarsIter;
	if (variableSpec != NULL && PgbsonInitIteratorAtPath(variableSpec, "let",
														 &letVarsIter))
	{
		ExpressionVariableContext *variableContext =
			palloc0(sizeof(ExpressionVariableContext));

		const bson_value_t *letVariables = bson_iter_value(&letVarsIter);
		ParseVariableSpec(letVariables, variableContext, &context);

		getState->variableContext = variableContext;
	}
}


/* Converts the {"": ["a", "b", "c"]} into a projection spec
 * of this form {"a": 1, "b": 1, "c": 1, "_id": 0}, so that the projection tree
 * can be created and cached as per requirement
 */
static void
CreateProjectionTreeStateForPartitionByFields(
	BsonExpressionPartitionByFieldsGetState *state, pgbson *partitionBy)
{
	if (partitionBy == NULL)
	{
		return;
	}

	pgbsonelement elem;
	PgbsonToSinglePgbsonElement(partitionBy, &elem);

	if (elem.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		return;
	}

	pgbson_writer projectionSpecWriter;
	PgbsonWriterInit(&projectionSpecWriter);
	bson_iter_t partitionValueItr;
	BsonValueInitIterator(&elem.bsonValue, &partitionValueItr);

	while (bson_iter_next(&partitionValueItr))
	{
		const bson_value_t *pathValue = bson_iter_value(&partitionValueItr);
		PgbsonWriterAppendInt32(&projectionSpecWriter, pathValue->value.v_utf8.str,
								pathValue->value.v_utf8.len, 1);
	}

	/* Exclude _id if any */
	PgbsonWriterAppendInt32(&projectionSpecWriter, "_id", 3, 0);

	pgbson *densifyPartitionProjectionSpec = PgbsonWriterGetPgbson(&projectionSpecWriter);
	bson_iter_t iter;
	PgbsonInitIterator(densifyPartitionProjectionSpec, &iter);

	bool forceProjectId = false;
	bool allowInclusionExclusion = true;
	state->projectionTreeState =
		(BsonProjectionQueryState *) GetProjectionStateForBsonProject(&iter,
																	  forceProjectId,
																	  allowInclusionExclusion);
}


/* This evaluates the varaible data and sets the result into the variableValue pointer.
 * We need to get an expressionResult to create a child expression and track the memory the evaluated document owns so that
 * it is not reused.
 * However, we can't use the variable context from the given parentExpressionResult since the variable data context is higher on the stack than the actual
 * expression result we're given for the expression we're evaluating. Here is an example, given the following let we will have this expression result tree:
 * ExpressionResult (N1): { variableContext = .vars = {}, .parent = NULL }
 *  $let: { vars: {"a": 1}, "in": { "$let": { vars: { "b": $$a }, "in": { "$add": [ "$$b", "$$a" ] } } } } } }
 *
 *      ChildExprResult (N2): ($let)
 *           variableContext = .vars = { "a": 1 }, .parent: N1
 *              ChildExprResult: ($let) N3
 *                  variableContext: = { .vars = { "b": "$$a" }, .parent = N2
 *                      ChildExprResult: ($add) N4
 *                          variableContext: {}, .parent = N3
 *
 *                              GetVariableValue("b") -> N4 (not found)
 *                                          -> N3 -> $$a
 *
 * The given expressionResult in this case would be N4, and the variableData lives in N3, so when we evaluate "$$a" expression we need to use N2 as its variable context.
 */
static bool
GetVariableValueFromData(const VariableData *variable, pgbson *currentDocument,
						 ExpressionResult *parentExpressionResult,
						 const ExpressionVariableContext *variableContext,
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

		/* Override the parent here as we need the parent for variable expressions to be the context's parent where it was defined. */
		childExpressionResult.expressionResultPrivate.variableContext.parent =
			variableContext;

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
												current->parent, variableValue);
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
												expressionResult, current->parent,
												variableValue);
			}
		}

		current = current->parent;
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
 * Function to add a variable expression to the given context
 */
static void
VariableContextSetVariableExpression(ExpressionVariableContext *variableContext,
									 const StringView *name,
									 AggregationExpressionData *variableExpression)
{
	VariableData newVariable =
	{
		.isConstant = false,
		.expression = variableExpression,
		.name = *name
	};

	VariableContextSetVariableData(variableContext, &newVariable);
}


/*
 * Sets a variable into the expression result's variable context, with the given name and value.
 */
void
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


/* Core helper function to validate a variable name which allows to configure if it can start with upper case or not.
 * Defining variables can't start with upper case.
 * Referencing a variable can start with upper case.
 */
static void
ValidateVariableNameCore(StringView name, bool allowStartWithUpper)
{
	if (name.length <= 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"empty variable names are not allowed")));
	}

	uint32_t i;
	for (i = 0; i < name.length; i++)
	{
		char current = name.string[i];
		if (i == 0 && isascii(current) && !islower(current) &&
			!IsOverridableSystemVariable(&name) &&
			(!isupper(current) || !allowStartWithUpper))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
								"'%s' starts with an invalid character for a user variable name",
								name.string)));
		}
		else if (isascii(current) && !isdigit(current) && !islower(current) &&
				 !isupper(current) && current != '_')
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
								"'%s' contains an invalid character for a variable name: '%c'",
								name.string, current)));
		}
	}
}


/* Helper function that validates variable name */
void
ValidateVariableName(StringView name)
{
	bool allowStartWithUpper = false;
	ValidateVariableNameCore(name, allowStartWithUpper);
}


/* Helper function that parses a variable spec and adds them to the given expression context. */
void
ParseVariableSpec(const bson_value_t *variableSpec,
				  ExpressionVariableContext *variableContext,
				  ParseAggregationExpressionContext *parseContext)
{
	Assert(variableSpec->value_type == BSON_TYPE_DOCUMENT);

	bson_iter_t varsIter;
	BsonValueInitIterator(variableSpec, &varsIter);
	while (bson_iter_next(&varsIter))
	{
		StringView varName = bson_iter_key_string_view(&varsIter);
		ValidateVariableName(varName);

		const bson_value_t *varValue = bson_iter_value(&varsIter);

		AggregationExpressionData *variableExpression = palloc0(
			sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(variableExpression, varValue, parseContext);

		/* if no context is provided, we just perform validation. */
		if (variableContext == NULL)
		{
			pfree(variableExpression);
		}
		else
		{
			VariableContextSetVariableExpression(variableContext, &varName,
												 variableExpression);
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


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

static inline void
ReportOperatorExpressonSyntaxError(const char *fieldA, bson_iter_t *fieldBIter, bool
								   performOperatorCheck)
{
	if (bson_iter_key_len(fieldBIter) == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), errmsg(
							"FieldPath cannot be constructed with empty string.")));
	}

	/* 1. If performOperatorCheck = false, time to throw error already */
	/* 2. Or, see if the iterator is pointing to an operator. If yes, throw error. */
	if (!performOperatorCheck || (bson_iter_key_len(fieldBIter) > 1 && bson_iter_key(
									  fieldBIter)[0] == '$'))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40181), errmsg(
							"an expression operator specification must contain exactly one field, found 2 fields '%s' and '%s'.",
							fieldA,
							bson_iter_key(fieldBIter))));
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
	if (value->value_type != BSON_TYPE_DOCUMENT &&
		value->value_type != BSON_TYPE_ARRAY)
	{
		return false;
	}

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


/* Process elements for operators which take at most three arguments eg, $slice, $range */
void
ProcessThreeArgumentElement(const bson_value_t *currentElement,
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
}


/* --------------------------------------------------------- */
/* New Aggregation Operator Parsing Framework functions      */
/* --------------------------------------------------------- */

/* Function that parses a given expression in the bson_value_t and populates the
 * expressionData with the expression information. */
void
ParseAggregationExpressionData(AggregationExpressionData *expressionData,
							   const bson_value_t *value,
							   ParseAggregationExpressionContext *context)
{
	/* Specific operators' parse functions will call into this function recursively to parse its arguments. */
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		ParseDocumentAggregationExpressionData(value, expressionData, context);
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		ParseArrayAggregationExpressionData(value, expressionData, context);
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
				else if (StringViewEndsWith(&expressionView, '.'))
				{
					/* We got a $$var. expression which is invalid. */
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
									errmsg("FieldPath must not end with a '.'.")));
				}

				if (StringViewEqualsCString(&expressionView, "$$NOW"))
				{
					/* currently handling $$NOW for find and aggregation only. */
					bson_value_t nowValue = context->timeSystemVariables.nowValue;
					if (nowValue.value_type == BSON_TYPE_EOD)
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
										errmsg(
											"$$NOW is not supported in this context.")));
					}

					if (dottedSuffix.length == 0)
					{
						expressionData->value = nowValue;
					}
					else
					{
						/* We set the value to EOD for dotted suffixes */
						expressionData->value.value_type = BSON_TYPE_EOD;
					}

					expressionData->kind = AggregationExpressionKind_Constant;
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
					if (context->allowRedactVariables)
					{
						expressionData->kind = AggregationExpressionKind_SystemVariable;
						expressionData->systemVariable.kind =
							AggregationExpressionSystemVariableKind_Descend;
						expressionData->systemVariable.pathSuffix = dottedSuffix;
					}
					else
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17276),
										errmsg("Use of undefined variable: DESCEND")));
					}
				}
				else if (StringViewEqualsCString(&expressionView, "$$PRUNE"))
				{
					if (context->allowRedactVariables)
					{
						expressionData->kind = AggregationExpressionKind_SystemVariable;
						expressionData->systemVariable.kind =
							AggregationExpressionSystemVariableKind_Prune;
						expressionData->systemVariable.pathSuffix = dottedSuffix;
					}
					else
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17276),
										errmsg("Use of undefined variable: PRUNE")));
					}
				}
				else if (StringViewEqualsCString(&expressionView, "$$KEEP"))
				{
					if (context->allowRedactVariables)
					{
						expressionData->kind = AggregationExpressionKind_SystemVariable;
						expressionData->systemVariable.kind =
							AggregationExpressionSystemVariableKind_Keep;
						expressionData->systemVariable.pathSuffix = dottedSuffix;
					}
					else
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17276),
										errmsg("Use of undefined variable: KEEP")));
					}
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
					/* Remove the $$ prefix. */
					StringView variableName = StringViewSubstring(&expressionView, 2);

					bool allowStartWithUpper = true;
					ValidateVariableNameCore(variableName, allowStartWithUpper);
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
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16411),
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

	if (context != NULL &&
		context->validateParsedExpressionFunc != NULL)
	{
		context->validateParsedExpressionFunc(expressionData);
	}
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
			/* We ensure it defines a handle function at the parsing layer. */
			Assert(expressionData->operator.handleExpressionFunc != NULL);

			expressionData->operator.handleExpressionFunc(document,
														  expressionData->operator.
														  arguments,
														  expressionResult);

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

			/* We ensure it defines a handle function at the parsing layer. */
			Assert(expressionData->operator.handleExpressionFunc != NULL);

			expressionData->operator.handleExpressionFunc(document,
														  expressionData->operator.
														  arguments,
														  &expressionResult);

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
			if (expressionData->value.value_type != BSON_TYPE_EOD)
			{
				pgbson_element_writer elementWriter;
				PgbsonInitObjectElementWriter(writer, &elementWriter, path.string,
											  path.length);
				PgbsonElementWriterWriteValue(&elementWriter, &expressionData->value);
			}
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
								 ParseAggregationExpressionContext *context)
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
		ParseAggregationExpressionData(argumentData, argumentValue, context);
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
		ParseAggregationExpressionData(argumentData, arg, context);
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
			ParseAggregationExpressionData(argData, arg, context);
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17276),
						errmsg("Use of undefined variable: %s",
							   CreateStringFromStringView(&varName))));
	}

	expressionResult->isFieldPathExpression = isDottedExpression;
	if (!isDottedExpression)
	{
		/* Don't write empty variable values. */
		if (variableValue.value_type != BSON_TYPE_EOD)
		{
			ExpressionResultSetValue(expressionResult, &variableValue);
		}

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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("Variable $$NOW not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_ClusterTime:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("Variable $$CLUSTER_TIME not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_Current:
		{
			if (!ExpressionResultGetVariable(CurrentVariableName, expressionResult,
											 document,
											 &variableValue))
			{
				variableValue = ConvertPgbsonToBsonValue(document);
			}

			break;
		}

		case AggregationExpressionSystemVariableKind_Remove:
		{
			return;
		}

		/* $$KEEP $$DESCEND $$PRUNE can only be used by stage $redact and gated by boolean allowRedactVariables.
		 * Return the variable string as we handle the logics at stage level.*/
		case AggregationExpressionSystemVariableKind_Descend:
		{
			variableValue.value_type = BSON_TYPE_UTF8;
			variableValue.value.v_utf8.str = "$$DESCEND";
			variableValue.value.v_utf8.len = 9;
			break;
		}

		case AggregationExpressionSystemVariableKind_Prune:
		{
			variableValue.value_type = BSON_TYPE_UTF8;
			variableValue.value.v_utf8.str = "$$PRUNE";
			variableValue.value.v_utf8.len = 7;
			break;
		}

		case AggregationExpressionSystemVariableKind_Keep:
		{
			variableValue.value_type = BSON_TYPE_UTF8;
			variableValue.value.v_utf8.str = "$$KEEP";
			variableValue.value.v_utf8.len = 6;
			break;
		}

		case AggregationExpressionSystemVariableKind_SearchMeta:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("Variable $$SEARCH_META not supported yet")));
		}

		case AggregationExpressionSystemVariableKind_UserRoles:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("Variable $$USER_ROLES not supported yet")));
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
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
									   ParseAggregationExpressionContext *parseContext)
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40181), errmsg(
								"an expression operator specification must contain exactly one field, found 2 fields '%s' and '%s'.",
								operatorKey,
								bson_iter_key(&docIter))));
		}

		expressionData->kind = AggregationExpressionKind_Operator;
		MongoOperatorExpression searchKey = {
			.operatorName = operatorKey,
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31325),
							errmsg("Unknown expression %s", searchKey.operatorName),
							errdetail_log("Unknown expression %s",
										  searchKey.operatorName)));
		}

		if (pItem->featureCounterId >= 0 && pItem->featureCounterId < MAX_FEATURE_INDEX)
		{
			ReportFeatureUsage(pItem->featureCounterId);
		}

		if (pItem->parseAggregationExpressionFunc != NULL)
		{
			pItem->parseAggregationExpressionFunc(argument, expressionData, parseContext);

			if (expressionData->kind == AggregationExpressionKind_Operator)
			{
				if (pItem->handlePreParsedOperatorFunc == NULL)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
									errmsg(
										"Operator %s doesn't specify a runtime handler function and doesn't resolve the operator evaluation at the parsing layer.",
										searchKey.operatorName),
									errdetail_log(
										"Operator %s doesn't specify a runtime handler function and doesn't resolve the operator evaluation at the parsing layer.",
										searchKey.operatorName)));
				}

				/* The parsed expression based on its behavior an arguments was converted to a constant, don't set
				 * the handler function so that when we evaluate this expression against each document we just return the constant value. */
				expressionData->operator.handleExpressionFunc =
					pItem->handlePreParsedOperatorFunc;

				Assert(expressionData->operator.argumentsKind !=
					   AggregationExpressionArgumentsKind_Invalid);
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("Operator %s not implemented yet",
								   searchKey.operatorName),
							errdetail_log("Operator %s not implemented yet",
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
	context.parseAggregationContext.collationString = parseContext->collationString;
	context.buildPathTreeFuncs = &DefaultPathTreeFuncs;
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
									ParseAggregationExpressionContext *parseContext)
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
											  treatLeafDataAsConstant);

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
																		   parseContext);
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
								 ParseAggregationExpressionContext *context)
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
		ParseAggregationExpressionData(argumentData, argumentValue, context);
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
			ParseAggregationExpressionData(argData, arg, context);
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
ParseDollarLiteral(const bson_value_t *inputDocument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context)
{
	data->kind = AggregationExpressionKind_Constant;
	data->value = *inputDocument;
}


/*
 * Evaluates the output of a $let expression.
 * $let is expressed as: { $let: { vars: {var1: <expression>, var2: <expression> }, in: <expression> }}
 */
void
ParseDollarLet(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16874), errmsg(
							"$let only supports an object as its argument")));
	}

	bson_iter_t docIter;
	BsonValueInitIterator(argument, &docIter);

	bson_value_t vars = { 0 };
	bson_value_t in = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "vars") == 0)
		{
			vars = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "in") == 0)
		{
			in = *bson_iter_value(&docIter);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16875), errmsg(
								"Unrecognized parameter to $let: %s", key),
							errdetail_log(
								"Unrecognized parameter to $let, unexpected key")));
		}
	}

	if (vars.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16876), errmsg(
							"Missing 'vars' parameter to $let")));
	}

	if (in.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16876), errmsg(
							"Missing 'in' parameter to $let")));
	}

	if (vars.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION10065), errmsg(
							"invalid parameter: expected an object (vars)")));
	}

	AggregationExpressionData *inData = palloc0(sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(inData, &in, context);

	bool isInConstant = inData->kind == AggregationExpressionKind_Constant;

	/* if in is constant, there's no point on creating the hashtable, we just parse for validity. */
	ExpressionVariableContext *inputVariableContext =
		isInConstant ? NULL : palloc0(sizeof(ExpressionVariableContext));

	ParseVariableSpec(&vars, inputVariableContext, context);

	if (isInConstant)
	{
		data->kind = AggregationExpressionKind_Constant;
		data->value = in;
		pfree(inData);
		return;
	}

	data->operator.arguments = list_make2(inData, inputVariableContext);
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
}


/* Handles a pre parsed $let aggregation operator.
 * The arguments is a list(2) where:
 *    list[0] is the in argument to evaluate with the given variables.
 *    list[1] is the parsed variable context for in.
 *
 * We evaluate in with the given variable context and return the result of that evaluation. */
void
HandlePreParsedDollarLet(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	List *argList = (List *) arguments;

	AggregationExpressionData *inExpression = list_nth(argList, 0);
	const ExpressionVariableContext *inputVariableContext = list_nth(argList, 1);

	/* let is a special aggregator operator which needs to insert a variable context,
	 * but it is not ideal to expose a shared API to modify the variable context for the expression result, as it could lead to wrong usage.
	 * instead do it inline. */
	ExpressionResult childExpressionResult = ExpressionResultCreateWithTracker(
		expressionResult->expressionResultPrivate.tracker);
	childExpressionResult.expressionResultPrivate.variableContext = *inputVariableContext;
	childExpressionResult.expressionResultPrivate.variableContext.parent =
		&expressionResult->expressionResultPrivate.variableContext;

	bool isNullOnEmpty = false;
	EvaluateAggregationExpressionData(inExpression, doc, &childExpressionResult,
									  isNullOnEmpty);

	/* If the result is a not existing path we should not set it. */
	if (childExpressionResult.value.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &childExpressionResult.value);
	}
}


/* Parses the expressions that are specified as arguments for an operator and returns
 * the list of AggregationExpressionData arguments */
List *
ParseVariableArgumentsForExpression(const bson_value_t *value, bool *isConstant,
									ParseAggregationExpressionContext *context)
{
	List *resultList = NIL;
	*isConstant = true;

	if (value->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);

		while (bson_iter_next(&arrayIter))
		{
			const bson_value_t *current = bson_iter_value(&arrayIter);
			AggregationExpressionData *expressionData = palloc0(
				sizeof(AggregationExpressionData));
			ParseAggregationExpressionData(expressionData, current, context);

			if (!IsAggregationExpressionConstant(expressionData))
			{
				*isConstant = false;
			}

			resultList = lappend(resultList, expressionData);
		}
	}
	else
	{
		AggregationExpressionData *expressionData = palloc0(
			sizeof(AggregationExpressionData));
		ParseAggregationExpressionData(expressionData, value, context);

		if (!IsAggregationExpressionConstant(expressionData))
		{
			*isConstant = false;
		}

		resultList = lappend(resultList, expressionData);
	}
	return resultList;
}


/* Call back function for top level command let parsing to disallow path expressions, CURRENT and ROOT for a top level variable spec. */
static void
DisallowExpressionsForTopLevelLet(AggregationExpressionData *parsedExpression)
{
	/* Path expressions, CURRENT and ROOT are not allowed in command level let. */
	if (parsedExpression->kind == AggregationExpressionKind_Path ||
		(parsedExpression->kind == AggregationExpressionKind_SystemVariable &&
		 (parsedExpression->systemVariable.kind ==
		  AggregationExpressionSystemVariableKind_Current ||
		  parsedExpression->systemVariable.kind ==
		  AggregationExpressionSystemVariableKind_Root)))
	{
		ereport(ERROR, errcode(ERRCODE_DOCUMENTDB_LOCATION4890500), errmsg(
					"Command let Expression tried to access a field,"
					" but this is not allowed because command let expressions"
					" run before the query examines any documents."));
	}
}


/* Stores the value of $$NOW from the variableSpec in timeSystemVariables. */
void
GetTimeSystemVariablesFromVariableSpec(pgbson *variableSpec,
									   TimeSystemVariables *timeSystemVariables)
{
	if (!EnableNowSystemVariable || variableSpec == NULL)
	{
		return;
	}

	bson_iter_t iter;
	if (PgbsonInitIteratorAtPath(variableSpec, "now", &iter))
	{
		const bson_value_t *nowDateValue = bson_iter_value(&iter);
		timeSystemVariables->nowValue = *nowDateValue;
	}
}


/*
 * Get the value of the $$NOW time system variable.
 * If the value is already generated (i.e. timeSystemVariables != NULL) re-use value.
 * Else generate it and store in the 'timeSystemVariables' field of the cursor queryData also.
 * This will allow for use in cursor_get_more queries.
 */
static bson_value_t
GetTimeSystemVariables(TimeSystemVariables *timeVariables)
{
	bson_value_t nowValue = { 0 };
	if (timeVariables != NULL &&
		timeVariables->nowValue.value_type != BSON_TYPE_EOD)
	{
		nowValue = timeVariables->nowValue;
	}
	else
	{
		nowValue.value_type = BSON_TYPE_DATE_TIME;
		TimestampTz timestamp = GetCurrentTransactionStartTimestamp();
		nowValue.value.v_datetime = GetDateTimeFromTimestamp(timestamp);

		if (timeVariables != NULL)
		{
			timeVariables->nowValue = nowValue;
		}
	}

	return nowValue;
}


/*
 * Parses a top level command let i.e: find or aggregate let spec, and time system variables.
 * 1) Path expressions are not valid at this scope because there's no doc to evaluate against.
 * 2) Variable references are valid only if they reference a variable that is defined previously in the same let spec, i.e: {a: 1, b: "$$a", c: {$add: ["$$a", "$$b"]}}
 * or a variable that is a system variable, i.e: {a: "$$NOW"}
 *
 * Given these 2 rules, we evaluate every variable expression we find against an empty document, using the current variable spec we're building to evaluate it.
 * As we evaluate expressions we rewrite the variable spec into a constant bson and return it.
 * The example in item 2, would be rewritten to: {"let": { a: 1, b: 1, c: 2 } }.
 *
 * If EnableNowSystemVariable, the example in item 2 would be rewritten to: {"now": <current time>, "let": { a: 1, b: 1, c: 2 } }.
 */
pgbson *
ParseAndGetTopLevelVariableSpec(const bson_value_t *varSpec,
								TimeSystemVariables *timeSystemVariables)
{
	ParseAggregationExpressionContext parseContext = {
		.validateParsedExpressionFunc = &DisallowExpressionsForTopLevelLet,
	};

	pgbson_writer resultWriter;
	PgbsonWriterInit(&resultWriter);

	/* Write the time system variables */
	if (EnableNowSystemVariable && IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		bson_value_t nowVariableValue = GetTimeSystemVariables(timeSystemVariables);
		PgbsonWriterAppendValue(&resultWriter, "now", 3, &nowVariableValue);
		parseContext.timeSystemVariables.nowValue = nowVariableValue;
	}

	ExpressionVariableContext varContext = { 0 };
	pgbson *emptyDoc = NULL;
	StringView path = { .string = "", .length = 0 };
	bool isNullOnEmpty = false;

	/* Write the let variables. */
	if (varSpec != NULL && varSpec->value_type != BSON_TYPE_EOD)
	{
		/* Since path expressions are not allowed in this variable spec, we can evaluate them and transform
		 * the spec to a constant bson. To evaluate them we use an empty document as the document we evaluate the expressions against. */
		emptyDoc = PgbsonInitEmpty();

		pgbson_writer letVarsWriter;
		PgbsonWriterStartDocument(&resultWriter, "let", 3, &letVarsWriter);

		bson_iter_t varsIter;
		BsonValueInitIterator(varSpec, &varsIter);
		while (bson_iter_next(&varsIter))
		{
			StringView varName = bson_iter_key_string_view(&varsIter);
			ValidateVariableName(varName);

			const bson_value_t *varValue = bson_iter_value(&varsIter);

			AggregationExpressionData expressionData = { 0 };
			ParseAggregationExpressionData(&expressionData, varValue, &parseContext);

			bson_value_t valueToWrite = { 0 };
			if (expressionData.kind != AggregationExpressionKind_Constant)
			{
				pgbson_writer exprWriter;
				PgbsonWriterInit(&exprWriter);
				EvaluateAggregationExpressionDataToWriter(&expressionData, emptyDoc, path,
														  &exprWriter, &varContext,
														  isNullOnEmpty);

				pgbson *evaluatedBson = PgbsonWriterGetPgbson(&exprWriter);

				if (!IsPgbsonEmptyDocument(evaluatedBson))
				{
					pgbsonelement element = { 0 };
					PgbsonToSinglePgbsonElement(evaluatedBson, &element);
					valueToWrite = element.bsonValue;
				}
			}
			else
			{
				valueToWrite = expressionData.value;
			}

			/* if it evaluates to an empty document let's convert to $$REMOVE so that we don't need to evaluate operators again and just treat it as EOD. */
			if (valueToWrite.value_type == BSON_TYPE_EOD)
			{
				valueToWrite.value_type = BSON_TYPE_UTF8;
				valueToWrite.value.v_utf8.str = "$$REMOVE";
				valueToWrite.value.v_utf8.len = 8;
				PgbsonWriterAppendValue(&letVarsWriter, varName.string, varName.length,
										&valueToWrite);
			}
			else
			{
				/* To write it to the result we need to wrap all expressions around a $literal, so that when the spec is parsed down level they are treated as constants
				 * if it encounters something that was a result of the expression that could be interpreted as non-constant. i.e $concat: ["$", "field"] -> "$field"
				 * We should parse that as a literal $field down level, rather than a field expression.
				 * However to insert it in the temp context, we should preserve the evaluated value to get correctnes if we have a case where a variable is used in operators within the same let.
				 * i.e {"a": "2", "b": {"$sum": ["$$a", 2]}} we want sum to get "2" when it evaluates the variable $$a reference instead of getting { "$literal": "2" }. */
				pgbson_writer literalWriter;
				PgbsonWriterStartDocument(&letVarsWriter, varName.string, varName.length,
										  &literalWriter);
				PgbsonWriterAppendValue(&literalWriter, "$literal", 8, &valueToWrite);
				PgbsonWriterEndDocument(&letVarsWriter, &literalWriter);
			}

			VariableData variableData = {
				.name = varName,
				.isConstant = true,
				.bsonValue = valueToWrite,
			};

			VariableContextSetVariableData(&varContext, &variableData);
		}

		PgbsonWriterEndDocument(&resultWriter, &letVarsWriter);

		pfree(emptyDoc);
	}

	if (varContext.context.table != NULL && !varContext.hasSingleVariable)
	{
		hash_destroy(varContext.context.table);
	}

	return PgbsonWriterGetPgbson(&resultWriter);
}
