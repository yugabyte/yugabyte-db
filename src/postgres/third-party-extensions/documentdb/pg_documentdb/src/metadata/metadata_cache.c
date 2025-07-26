/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/metadata/metadata_cache.c
 *
 * Implementation of general metadata caching functions.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <access/genam.h>
#include <access/table.h>
#include <catalog/pg_extension.h>
#include <catalog/pg_proc.h>
#include <catalog/pg_type.h>
#include <utils/fmgroids.h>

#include "commands/extension.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "nodes/makefuncs.h"
#include "parser/parse_func.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/version_utils.h"
#include "catalog/pg_am.h"

#include "metadata/metadata_cache.h"
#include "metadata/collection.h"
#include "commands/defrem.h"


#define PG_EXTENSION_NAME_SCAN_NARGS 1

#define RUM_EXTENSION_SCHEMA "public"

/*
 * CacheValidityValue represents the possible states of the cache.
 */
typedef enum CacheValidityValue
{
	/* cache was not succesfully initialized */
	CACHE_INVALID,

	/* extension does not exist, nothing to cache */
	CACHE_VALID_NO_EXTENSION,

	/* extension exist, cache is valid */
	CACHE_VALID
} CacheValidityValue;


static void InvalidateDocumentDBApiCache(Datum argument, Oid relationId);
static Oid GetBinaryOperatorId(Oid *operatorId, Oid leftTypeOid, char *operatorName,
							   Oid rightTypeOid);
static Oid GetInternalBinaryOperatorId(Oid *operatorId, Oid leftTypeOid,
									   char *operatorName,
									   Oid rightTypeOid);
static Oid GetCoreBinaryOperatorId(Oid *operatorId, Oid leftTypeOid, char *operatorName,
								   Oid rightTypeOid);
static Oid GetBinaryOperatorFunctionIdWithSchema(Oid *operatorFuncId, char *operatorName,
												 Oid leftTypeOid, Oid rightTypeOid,
												 char *schemaName);
static Oid GetBinaryOperatorFunctionId(Oid *operatorFuncId, char *operatorName,
									   Oid leftTypeOid, Oid rightTypeOid);
static Oid GetInternalBinaryOperatorFunctionId(Oid *operatorFuncId, char *operatorName,
											   Oid leftTypeOid, Oid rightTypeOid);
static Oid GetOperatorFunctionIdThreeArgs(Oid *operatorFuncId, char *schemaName,
										  char *operatorName,
										  Oid arg0TypeOid, Oid arg1TypeOid, Oid
										  arg2TypeOid);
static Oid GetOperatorFunctionIdFourArgs(Oid *operatorFuncId, char *schemaName,
										 char *operatorName,
										 Oid arg0TypeOid, Oid arg1TypeOid, Oid
										 arg2TypeOid,
										 Oid arg3TypeOid);
static Oid GetOperatorFunctionIdFiveArgs(Oid *operatorFuncId, char *schemaName,
										 char *operatorName,
										 Oid arg0TypeOid, Oid arg1TypeOid, Oid
										 arg2TypeOid, Oid arg3TypeOid, Oid arg4TypeId);
static Oid GetDocumentDBInternalBinaryOperatorFunctionId(Oid *operatorFuncId,
														 char *operatorName,
														 Oid leftTypeOid, Oid
														 rightTypeOid,
														 bool missingOk);
static Oid GetBinaryOperatorFunctionIdMissingOk(Oid *operatorFuncId, char *operatorName,
												Oid leftTypeOid, Oid rightTypeOid,
												const char *releaseName);
static Oid GetPostgresInternalFunctionId(Oid *functionId, char *operatorName);
static Oid GetArrayTypeOid(Oid *arrayTypeId, Oid baseElem);

/* Utilities to get function within a schema with variable arguments */
static Oid GetSchemaFunctionIdWithNargs(Oid *functionId, char *schema,
										char *functionName, int nargs,
										Oid *argTypes, bool missingOk);

/* indicates whether the cache is valid, or needs to be reset */
static CacheValidityValue CacheValidity = CACHE_INVALID;

/* session-level memory context in which we keep all cached bytes */
MemoryContext DocumentDBApiMetadataCacheContext = NULL;

PGDLLEXPORT char *ApiDataSchemaName = "documentdb_data";
PGDLLEXPORT char *ApiAdminRole = "documentdb_admin_role";
PGDLLEXPORT char *ApiAdminRoleV2 = "documentdb_admin_role";
PGDLLEXPORT char *ApiReadOnlyRole = "documentdb_readonly_role";
PGDLLEXPORT char *ApiSchemaName = "documentdb_api";
PGDLLEXPORT char *ApiSchemaNameV2 = "documentdb_api";
PGDLLEXPORT char *ApiInternalSchemaName = "documentdb_api_internal";
PGDLLEXPORT char *ApiInternalSchemaNameV2 = "documentdb_api_internal";
PGDLLEXPORT char *ExtensionObjectPrefix = "documentdb";
PGDLLEXPORT char *ExtensionObjectPrefixV2 = "documentdb";
/*
 * YB: Upstream bug(#164). AddressSanitizer: odr-violation. Already declared in type_cache.c.
PGDLLEXPORT char *CoreSchemaName = "documentdb_core";
PGDLLEXPORT char *CoreSchemaNameV2 = "documentdb_core";
 */
PGDLLEXPORT char *FullBsonTypeName = "documentdb_core.bson";
PGDLLEXPORT char *ApiExtensionName = "documentdb";
PGDLLEXPORT char *ApiCatalogSchemaName = "documentdb_api_catalog";
PGDLLEXPORT char *ApiCatalogSchemaNameV2 = "documentdb_api_catalog";
PGDLLEXPORT char *ApiGucPrefix = "documentdb";
PGDLLEXPORT char *PostgisSchemaName = "public";

/* Schema functions migrated from a public API to an internal API schema
 * (e.g. from documentdb_api -> documentdb_api_internal)
 * TODO: These should be transition and removed in subsequent releases.
 */
PGDLLEXPORT char *ApiToApiInternalSchemaName = "documentdb_api_internal";

PGDLLEXPORT char *ApiCatalogToApiInternalSchemaName = "documentdb_api_internal";

PGDLLEXPORT char *DocumentDBApiInternalSchemaName = "documentdb_api_internal";

PGDLLEXPORT char *ApiCatalogToCoreSchemaName = "documentdb_core";

typedef struct DocumentDBApiOidCacheData
{
	/* OID of the <bigint> OPERATOR(pg_catalog.=) <bigint> operator */
	Oid BigintEqualOperatorId;

	/* OID of the <text> OPERATOR(pg_catalog.=) <text> operator */
	Oid TextEqualOperatorId;

	/* OID of the <text> OPERATOR(pg_catalog.<>) <text> operator */
	Oid TextNotEqualOperatorId;

	/* OID of the <text> OPERATOR(pg_catalog.<) <text> operator */
	Oid TextLessOperatorId;

	/* OID of the vector type */
	Oid VectorTypeId;

	/* OID of the index_spec_type */
	Oid IndexSpecTypeId;

	/* OID of the ApiCatalogSchemaName.collections */
	Oid MongoCatalogCollectionsTypeOid;

	/* OID of the <bson> OPERATOR(ApiCatalogSchemaName.=) <bson> operator */
	Oid BsonEqualOperatorId;

	/* OID of the <bson> OPERATOR(ApiCatalogSchemaName.@=) <bson> operator */
	Oid BsonEqualMatchOperatorId;

	/* OID of the <bson> OPERATOR(ApiCatalogSchemaName.@*=) <bson> operator */
	Oid BsonInOperatorId;

	/* OID of the ApiSchemaName.bson_query_match() function */
	Oid BsonQueryMatchFunctionId;

	/* OID of the <bson> OPERATOR(ApiCatalogSchemaName.@@) <bson> operator */
	Oid BsonQueryOperatorId;

	/* OID of the bson_true_match function */
	Oid BsonTrueFunctionId;

	/* OID of the bson_empty_data_table function */
	Oid BsonEmptyDataTableFunctionId;

	/* OID of the coll_stats_aggregation function */
	Oid CollStatsAggregationFunctionOid;

	/* OID of the index_stats_aggregation function */
	Oid IndexStatsAggregationFunctionOid;

	/* OID of the current_op aggregation function */
	Oid BsonCurrentOpAggregationFunctionId;

	/* OID of the ApiSchemaName.list_indexes function */
	Oid IndexSpecAsBsonFunctionId;

	/* OID of the TABLESAMPLE SYSTEM_ROWS(n) function */
	Oid ExtensionTableSampleSystemRowsFunctionId;

	/* OID of the bson_in_range_interval function (in_range support function for btree to support RANGE in PARTITION clause) */
	Oid BsonInRangeIntervalFunctionId;

	/* OID of the bson_in_range_numeric function (in_range support function for btree to support RANGE in PARTITION clause) */
	Oid BsonInRangeNumericFunctionId;

	/* OID of ApiSchemaName.collection() UDF */
	Oid CollectionFunctionId;

	/* OID of the ApiSchemaName.collection() UDF */
	Oid DocumentDBApiCollectionFunctionId;

	/* OID of ApiSchemaName.create_indexes() UDF */
	Oid CreateIndexesProcedureId;

	/* OID of ApiSchemaName.re_index() UDF */
	Oid ReindexProcedureId;

	/* OID of ApiCatalogSchemaName.collections table */
	Oid CollectionsTableId;

	/* OID of collections_collection_id_seq sequence */
	Oid CollectionIdSequenceId;

	/* OID of collection_indexes_index_id_seq sequence */
	Oid CollectionIndexIdSequenceId;

	/* OID of ApiCatalogSchemaName schema */
	Oid MongoCatalogNamespaceId;

	/* OID of the current extension */
	Oid DocumentDBApiExtensionId;

	/* OID of the owner of the current extension */
	Oid DocumentDBApiExtensionOwner;

	/* OID of the bson_orderby function */
	Oid BsonOrderByFunctionId;

	/* OID of the bson_orderby_partition function */
	Oid BsonOrderByPartitionFunctionOid;

	/* OID of the bson vector search orderby operator */
	Oid VectorOrderByQueryOperatorId;

	/* OID of the pg_vector cosine similarity operator */
	Oid VectorCosineSimilaritySearchOperatorId;

	/* OID of the pg_vector l2 similarity operator */
	Oid VectorL2SimilaritySearchOperatorId;

	/* OID of the pg_vector ip similarity operator */
	Oid VectorIPSimilaritySearchOperatorId;

	/* OID of the cosine_distance function */
	Oid VectorCosineSimilaritySearchFunctionId;

	/* OID of the l2_distance function */
	Oid VectorL2SimilaritySearchFunctionId;

	/* OID of the vector_negative_inner_product function */
	Oid VectorIPSimilaritySearchFunctionId;

	/* OID of the pg_vector ivfflat cosine similarity operator */
	Oid VectorIVFFlatCosineSimilarityOperatorFamilyId;

	/* OID of the pg_vector hnsw cosine similarity operator */
	Oid VectorHNSWCosineSimilarityOperatorFamilyId;

	/* OID of the pg_vector ivfflat l2 similarity operator */
	Oid VectorIVFFlatL2SimilarityOperatorFamilyId;

	/* OID of the pg_vector hnsw l2 similarity operator */
	Oid VectorHNSWL2SimilarityOperatorFamilyId;

	/* OID of the pg_vector ivfflat ip similarity operator */
	Oid VectorIVFFlatIPSimilarityOperatorFamilyId;

	/* OID of the pg_vector hnsw ip similarity operator */
	Oid VectorHNSWIPSimilarityOperatorFamilyId;

	/* OID of the <float8> + <float8> operator */
	Oid Float8PlusOperatorId;

	/* OID of the <float8> - <float8> operator */
	Oid Float8MinusOperatorId;

	/* OID of the <float8> * <float8> operator */
	Oid Float8MultiplyOperatorId;

	/* OID of gin_bson_exclusion_pre_consistent function */
	Oid BsonExclusionPreconsistentFunctionId;

	/* OID of the greater than '>' operator for bson */
	Oid BsonGreaterThanOperatorId;

	/* OID of the less than '>=' operator for bson */
	Oid BsonGreaterThanEqualOperatorId;

	/* OID of the less than '<' operator for bson */
	Oid BsonLessThanOperatorId;

	/* OID of the less than '<=' operator for bson */
	Oid BsonLessThanEqualOperatorId;

	/* OID of the $eq function for bson query */
	Oid BsonEqualMatchRuntimeFunctionId;

	/* Oid of the $eq runtime operator #= */
	Oid BsonEqualMatchRuntimeOperatorId;

	/* OID of the $eq function for bson index */
	Oid BsonEqualMatchIndexFunctionId;

	/* OID of the $gt function for bson query */
	Oid BsonGreaterThanMatchRuntimeFunctionId;

	/* Oid of the $gt runtime operator #> */
	Oid BsonGreaterThanMatchRuntimeOperatorId;

	/* OID of the $gt function for bson index */
	Oid BsonGreaterThanMatchIndexFunctionId;

	/* OID of the $gte function for bson query */
	Oid BsonGreaterThanEqualMatchRuntimeFunctionId;

	/* Oid of the $gte runtime operator #>= */
	Oid BsonGreaterThanEqualMatchRuntimeOperatorId;

	/* OID of the $gte function for bson index */
	Oid BsonGreaterThanEqualMatchIndexFunctionId;

	/* OID of the $lt function for bson query */
	Oid BsonLessThanMatchRuntimeFunctionId;

	/* Oid of the $lt runtime operator #< */
	Oid BsonLessThanMatchRuntimeOperatorId;

	/* OID of the $lt function for bson index */
	Oid BsonLessThanMatchIndexFunctionId;

	/* OID of the $lte function for bson query */
	Oid BsonLessThanEqualMatchRuntimeFunctionId;

	/* Oid of the $lte runtime operator #<= */
	Oid BsonLessThanEqualMatchRuntimeOperatorId;

	/* OID of the $lte function for bson index */
	Oid BsonLessThanEqualMatchIndexFunctionId;

	/* Oid of the bson_dollar_range function */
	Oid BsonRangeMatchFunctionId;

	/* Oid of the $range runtime operator #<> */
	Oid BsonRangeMatchOperatorOid;

	/* OID of the $not: { $lte: {} } function */
	Oid BsonNotLessThanEqualFunctionId;

	/* OID of the $not: { $lt: {} } function */
	Oid BsonNotLessThanFunctionId;

	/* OID of the $not: { $gte: {} } function */
	Oid BsonNotGreaterThanEqualFunctionId;

	/* OID of the $not: { $gt: {} } function */
	Oid BsonNotGreaterThanFunctionId;

	/* OID of the $in function for bson */
	Oid BsonInMatchFunctionId;

	/* OID of the $nin function for bson */
	Oid BsonNinMatchFunctionId;

	/* OID of the $ne function for bson */
	Oid BsonNotEqualMatchFunctionId;

	/* OID of the $all function for bson */
	Oid BsonAllMatchFunctionId;

	/* OID of the $elemMatch function for bson */
	Oid BsonElemMatchMatchFunctionId;

	/* OID of the $regex function for bson */
	Oid BsonRegexMatchFunctionId;

	/* OID of the $mod function for bson */
	Oid BsonModMatchFunctionId;

	/* OID of the $size function for bson */
	Oid BsonSizeMatchFunctionId;

	/* OID of the $type function for bson */
	Oid BsonTypeMatchFunctionId;

	/* OID of the command_bson_get_value function */
	Oid BsonGetValueFunctionId;

	/* OID of the $exists function for bson */
	Oid BsonExistsMatchFunctionId;

	/* OID of the cursor state function */
	Oid CursorStateFunctionId;

	/* OID of the current curor state function */
	Oid CurrentCursorStateFunctionId;

	/* OID of the $bitsAllClear function for bson */
	Oid BsonBitsAllClearFunctionId;

	/* OID of the $bitsAnyClear function for bson */
	Oid BsonBitsAnyClearFunctionId;

	/* OID of the $bitsAllSet function for bson */
	Oid BsonBitsAllSetFunctionId;

	/* OID of the $bitsAnySet function for bson */
	Oid BsonBitsAnySetFunctionId;

	/* OID of the $expr function for bson */
	Oid BsonExprFunctionId;

	/* OID of the $jsonSchema function for bson */
	Oid BsonJsonSchemaFunctionId;

	/* OID of the $expr function for bson with let */
	Oid BsonExprWithLetFunctionId;

	/* OID of the $text function for bson */
	Oid BsonTextFunctionId;

	/* OID of the $eq function function for bson_values */
	Oid BsonValueEqualMatchFunctionId;

	/* OID of the <bson> ##= <bsonindexbounds> operator */
	Oid BsonIndexBoundsEqualOperatorId;

	/* OID of the bson_dollar_eq(<bson>, <bsonindexbounds>) */
	Oid BsonIndedBoundsEqualOperatorFuncId;

	/* OID of the $gt function function for bson_values */
	Oid BsonValueGreaterMatchFunctionId;

	/* OID of the $gte function function for bson_values */
	Oid BsonValueGreaterEqualMatchFunctionId;

	/* OID of the $lt function function for bson_values */
	Oid BsonValueLessMatchFunctionId;

	/* OID of the $lte function function for bson_values */
	Oid BsonValueLessEqualMatchFunctionId;

	/* OID of the $size function function for bson_values */
	Oid BsonValueSizeMatchFunctionId;

	/* OID of the $type function function for bson_values */
	Oid BsonValueTypeMatchFunctionId;

	/* OID of the $in function function for bson_values */
	Oid BsonValueInMatchFunctionId;

	/* OID of the $nin function function for bson_values */
	Oid BsonValueNinMatchFunctionId;

	/* OID of the $ne function function for bson_values */
	Oid BsonValueNotEqualMatchFunctionId;

	/* OID of the $exists function function for bson_values */
	Oid BsonValueExistsMatchFunctionId;

	/* OID of the $elemMatch function for bson_values */
	Oid BsonValueElemMatchMatchFunctionId;

	/* OID of the $all function for bson_values */
	Oid BsonValueAllMatchFunctionId;

	/* OID of the $regex function function for bson_values */
	Oid BsonValueRegexMatchFunctionId;

	/* OID of the $mod function for bson_values */
	Oid BsonValueModMatchFunctionId;

	/* OID of the $bitsAllClear function function for bson_values */
	Oid BsonValueBitsAllClearFunctionId;

	/* OID of the $bitsAnyClear function function for bson_values */
	Oid BsonValueBitsAnyClearFunctionId;

	/* OID of the $bitsAllSet function function for bson_values */
	Oid BsonValueBitsAllSetFunctionId;

	/* OID of the $bitsAnySet function function for bson_values */
	Oid BsonValueBitsAnySetFunctionId;

	/* Oid of the bson_densify_range window function */
	Oid BsonDensifyRangeWindowFunctionOid;

	/* Oid of the bson_densify_partition window function */
	Oid BsonDensifyPartitionWindowFunctionOid;

	/* Oid of the bson_densify_full window function */
	Oid BsonDensifyFullWindowFunctionOid;

	/* OID of the drandom postgres method which generates a random float number in range [0 - 1) */
	Oid PostgresDrandomFunctionId;

	/* OID of the float8_timestamptz postgres method which generates a timestamp from a unix epoch in seconds */
	Oid PostgresToTimestamptzFunctionId;

	/* OID of the date_part postgres method which get's a specific unit part of a date */
	Oid PostgresDatePartFunctionId;

	/* OID of the timestamptz_zone postgres method which shifts the current timestamp to the specified timezone */
	Oid PostgresTimestampToZoneFunctionId;

	/* OID of the int4 + int4 function */
	Oid PostgresInt4PlusFunctionOid;

	/* OID of the int4 < int4 operator */
	Oid PostgresInt4LessOperatorOid;

	/* OID of the int4 < int4 function */
	Oid PostgresInt4LessOperatorFunctionOid;

	/* OID of the int4 = int4 function */
	Oid PostgresInt4EqualOperatorOid;

	/* OID of the make_interval postgres method which creates interval from taking in date part units.*/
	Oid PostgresMakeIntervalFunctionId;

	/* OID of the timestamp_pl_interval postgres method which increments timestamp with given interval size.*/
	Oid PostgresAddIntervalToTimestampFunctionId;

	/* OID of the date_pl_interval postgres method which increments timestamp with given interval size.*/
	Oid PostgresAddIntervalToDateFunctionId;

	/* OID of the timestamp_zone postgres method which shifts the current timestamp to the specified timezone.*/
	Oid PostgresTimestampToZoneWithoutTzFunctionId;

	/* OID of the to_date postgres method which given a formatted text and string date gives you date object.*/
	Oid PostgresToDateFunctionId;

	/* OID of the float8 = float8 operator */
	Oid Float8EqualOperatorId;

	/* OID of the float8 <= float8 operator */
	/* TODO: Remove this once geonear range operator is defined and this is not used */
	Oid Float8LessThanEqualOperatorId;

	/* OID of the float8 >= float8 operator */
	Oid Float8GreaterThanEqualOperatorId;

	/* OID of the array_append postgres function */
	Oid PostgresArrayAppendFunctionOid;

	/* Oid of the timestamptz_bin postgres method which gives timestamp for the bin input into specified interval aligned with specified origin. */
	Oid PostgresDateBinFunctionId;

	/* OID of the timestamp_age postgres method which gives the age betwwen 2 timestamp without zone. */
	Oid PostgresAgeBetweenTimestamp;

	/* Oid of the extract_interval postgres function which extracts a given date part from interval. */
	Oid PostgresDatePartFromInterval;

	/* OID of the uuid_in postgres method which converts a string to uuid */
	Oid PostgresUUIDInFunctionIdOid;

	/* OID of Rum Index access methods */
	Oid RumIndexAmId;

	/* OID Of the vector ivfflat index access methods */
	Oid PgVectorIvfFlatIndexAmId;

	/* OID Of the vector hnsw index access methods */
	Oid PgVectorHNSWIndexAmId;

	/* OID of the array_to_vector function. */
	Oid PgDoubleToVectorFunctionOid;

	/* OID of the vector as vector Cast function */
	Oid VectorAsVectorFunctionOid;

	/* OID of the bson_extract_vector function from a document and path */
	Oid ApiCatalogBsonExtractVectorFunctionId;

	/* OID of the bson_search_param function to wrap search parameter. */
	Oid ApiBsonSearchParamFunctionId;

	/* OID of the bson_document_add_score_field function add vector score to document */
	Oid ApiBsonDocumentAddScoreFieldFunctionId;

	/* OID of the websearch_to_tsquery function. */
	Oid WebSearchToTsQueryFunctionId;

	/* OID of the websearch_to_tsquery function with regconfig option. */
	Oid WebSearchToTsQueryWithRegConfigFunctionId;

	/* OID of the rum_extract_tsvector function */
	Oid RumExtractTsVectorFunctionId;

	/* OID of the operator class for BSON Text operations with {ExtensionObjectPrefix}_rum */
	Oid BsonRumTextPathOperatorFamily;

	/* OID of the operator class for BSON GIST geography */
	Oid BsonGistGeographyOperatorFamily;

	/* OID of the operator class for BSON GIST geometry */
	Oid BsonGistGeometryOperatorFamily;

	/* OID of the operator class for BSON Single Path operations with {ExtensionObjectPrefix}_rum */
	Oid BsonRumSinglePathOperatorFamily;

	/* OID of the bson_text_meta_qual function ID */
	Oid BsonTextSearchMetaQualFuncId;

	/* OID of the ts_rank function id */
	Oid PostgresTsRankFunctionId;

	/* OID of the tsvector_concat function */
	Oid TsVectorConcatFunctionId;

	/* OID of the ts_match_vq function */
	Oid TsMatchFunctionOid;

	/* OID of the bson_aggregation_pipeline function */
	Oid ApiCatalogAggregationPipelineFunctionId;

	/* OID of the bson_aggregation_find function */
	Oid ApiCatalogAggregationFindFunctionId;

	/* OID of the bson_aggregation_count function */
	Oid ApiCatalogAggregationCountFunctionId;

	/* OID of the bson_aggregation_distinct function */
	Oid ApiCatalogAggregationDistinctFunctionId;

	/* OID of the BSONCOVARIANCEPOP aggregate function */
	Oid ApiCatalogBsonCovariancePopAggregateFunctionOid;

	/* OID of the BSONCOVARIANCESAMP aggregate function */
	Oid ApiCatalogBsonCovarianceSampAggregateFunctionOid;

	/* OID of the bson_dollar_add_fields function */
	Oid ApiCatalogBsonDollarAddFieldsFunctionOid;

	/* OID of the bson_dollar_add_fields with let function */
	Oid ApiCatalogBsonDollarAddFieldsWithLetFunctionOid;

	/* OID of the bson_dollar_add_fields with let and collation function */
	Oid ApiCatalogBsonDollarAddFieldsWithLetAndCollationFunctionOid;

	/* OID of the bson_dollar_inverse_match function */
	Oid ApiCatalogBsonDollarInverseMatchFunctionOid;

	/* OID OF the bson_dollar_merge_documents function */
	Oid ApiInternalSchemaBsonDollarMergeDocumentsFunctionOid;

	/* OID OF the bson_dollar_merge_documents_at_path function */
	Oid ApiInternalSchemaBsonDollarMergeDocumentAtPathFunctionOid;

	/* OID of the bson_dollar_merge_handle_when_matched function */
	Oid ApiInternalBsonDollarMergeHandleWhenMatchedFunctionId;

	/* OID of the bson_dollar_merge_join function */
	Oid ApiInternalBsonDollarMergeJoinFunctionId;

	/* OID of the bson_dollar_merge_add_object_id function */
	Oid ApiInternalBsonDollarMergeAddObjectIdFunctionId;

	/* OID of the bson_dollar_merge_generate_object_id function */
	Oid ApiInternalBsonDollarMergeGenerateObjectId;

	/* OID of the bson_dollar_extract_merge_filter function */
	Oid ApiInternalBsonDollarMergeExtractFilterFunctionId;

	/* OID of the bson_dollar_merge_fail_when_not_matched function */
	Oid ApiInternalBsonDollarMergeFailWhenNotMathchedFunctionId;

	/* OID of the bson_dollar_project function */
	Oid ApiCatalogBsonDollarProjectFunctionOid;

	/* OID of the bson_dollar_redact(bson, bson, text, bson) function */
	Oid ApiInternalBsonDollarRedactWithLetFunctionOid;

	/* OID of the bson_dollar_redact(bson, bson, text, bson, text) function */
	Oid ApiInternalBsonDollarRedactWithLetAndCollationFunctionOid;

	/* OID of the bson_dollar_project function with let */
	Oid ApiCatalogBsonDollarProjectWithLetFunctionOid;

	/* Oid of the bson_dollar_project function with let and collation */
	Oid ApiCatalogBsonDollarProjectWithLetAndCollationFunctionOid;

	/* OID of the bson_dollar_project_expression function */
	Oid ApiCatalogBsonDollarLookupExpressionEvalMergeOid;

	/* OID of the bson_dollar_project_find function */
	Oid ApiCatalogBsonDollarProjectFindFunctionOid;

	/* OID of the bson_dollar_project_find with let args function */
	Oid ApiCatalogBsonDollarProjectFindWithLetFunctionOid;

	/* OID of the bson_dollar_project_find with let and collation function */
	Oid ApiCatalogBsonDollarProjectFindWithLetAndCollationFunctionOid;

	/* OID of the bson_dollar_unwind(bson, text) function */
	Oid ApiCatalogBsonDollarUnwindFunctionOid;

	/* OID of the bson_dollar_unwind(bson, bson) function */
	Oid ApiCatalogBsonDollarUnwindWithOptionsFunctionOid;

	/* OID of the bson_dollar_replace_root function */
	Oid ApiCatalogBsonDollarReplaceRootFunctionOid;

	/* OID of the bson_dollar_replace_root with let function */
	Oid ApiCatalogBsonDollarReplaceRootWithLetFunctionOid;

	/* OID of the bson_dollar_replace_root with let and collation function  */
	Oid ApiCatalogBsonDollarReplaceRootWithLetAndCollationFunctionOid;

	/* OID of the bson_rank window function */
	Oid ApiCatalogBsonRankFunctionOid;

	/* OID of the bson_dense_rank window function */
	Oid ApiCatalogBsonDenseRankFunctionOid;

	/* OID of the bson_document_number window function */
	Oid ApiCatalogBsonDocumentNumberFunctionOid;

	/* OID of the bson_shift window function */
	Oid ApiCatalogBsonShiftFunctionOid;

	/* OID of the BSONSUM aggregate function */
	Oid ApiCatalogBsonSumAggregateFunctionOid;

	/* OID of the bson_linear_fill window function */
	Oid ApiCatalogBsonLinearFillFunctionOid;

	/* OID of the bson_locf_fill window function */
	Oid ApiCatalogBsonLocfFillFunctionOid;

	/* OID of the BSONINTEGRAL aggregate function */
	Oid ApiCatalogBsonIntegralAggregateFunctionOid;

	/* OID of the BSONDERIVATIVE aggregate function */
	Oid ApiCatalogBsonDerivativeAggregateFunctionOid;

	/* OID of the BSONAVERAGE aggregate function */
	Oid ApiCatalogBsonAverageAggregateFunctionOid;

	/* OID of the bson_array_agg function. TODO remove this in favor of the below. */
	Oid ApiCatalogBsonArrayAggregateFunctionOid;

	/* OID of the bson_array_agg function */
	Oid ApiCatalogBsonArrayAggregateAllArgsFunctionOid;

	/* OID of the mongo bson_distinct_agg function */
	Oid ApiCatalogBsonDistinctAggregateFunctionOid;

	/* OID of the bson_object_agg function */
	Oid ApiCatalogBsonObjectAggregateFunctionOid;

	/* OID of the bson_merge_objects_on_sorted function */
	Oid ApiCatalogBsonMergeObjectsOnSortedFunctionOid;

	/* OID of the bson_merge_objects function */
	Oid ApiCatalogBsonMergeObjectsFunctionOid;

	/* OID of the BSONEXPMOVINGAVG window function */
	Oid ApiCatalogBsonExpMovingAvgAggregateFunctionOid;

	/* OID of the BSONMAX aggregate function */
	Oid ApiCatalogBsonMaxAggregateFunctionOid;

	/* OID of the BSONMIN aggregate function */
	Oid ApiCatalogBsonMinAggregateFunctionOid;

	/* OID of the BSONFIRSTONSORTED aggregate function */
	Oid ApiCatalogBsonFirstOnSortedAggregateFunctionOid;

	/* OID of the BSONLASTONSORTED aggregate function */
	Oid ApiCatalogBsonLastOnSortedAggregateFunctionOid;

	/* OID of the BSONFIRSTONSORTED aggregate function */
	Oid BsonFirstOnSortedAggregateAllArgsFunctionOid;

	/* OID of the BSONLASTONSORTED aggregate function */
	Oid BsonLastOnSortedAggregateAllArgsFunctionOid;

	/* OID of the BSONFIRST aggregate function */
	Oid ApiCatalogBsonFirstAggregateFunctionOid;

	/* OID of the BSONLAST aggregate function */
	Oid ApiCatalogBsonLastAggregateFunctionOid;

	/* OID of the BSONFIRST aggregate function with 3 args */
	Oid BsonFirstAggregateAllArgsFunctionOid;

	/* OID of the BSONLAST aggregate function with 3 args */
	Oid BsonLastAggregateAllArgsFunctionOid;

	/* OID of the BSONFIRSTNONSORTED aggregate function */
	Oid ApiCatalogBsonFirstNOnSortedAggregateFunctionOid;

	/* OID of the BSONLASTNONSORTED aggregate function */
	Oid ApiCatalogBsonLastNOnSortedAggregateFunctionOid;

	/* OID of the BSONFIRSTNONSORTED aggregate function */
	Oid BsonFirstNOnSortedAggregateAllArgsFunctionOid;

	/* OID of the BSONLASTNONSORTED aggregate function */
	Oid BsonLastNOnSortedAggregateAllArgsFunctionOid;

	/* OID of the BSONFIRSTN aggregate function */
	Oid ApiCatalogBsonFirstNAggregateFunctionOid;

	/* OID of the BSONLASTN aggregate function */
	Oid ApiCatalogBsonLastNAggregateFunctionOid;

	/* OID of the BSONFIRSTN aggregate function with 4 args  */
	Oid BsonFirstNAggregateAllArgsFunctionOid;

	/* OID of the BSONLASTN aggregate function with 4 args */
	Oid BsonLastNAggregateAllArgsFunctionOid;

	/* OID of the bson_add_to_set function. */
	Oid ApiCatalogBsonAddToSetAggregateFunctionOid;

	/* OID of the bson_repath_and_build function */
	Oid ApiCatalogBsonRepathAndBuildFunctionOid;

	/* OID of the BSONSTDDEVPOP aggregate function */
	Oid ApiCatalogBsonStdDevPopAggregateFunctionOid;

	/* OID of the BSONMAXN aggregate function */
	Oid ApiCatalogBsonMaxNAggregateFunctionOid;

	/* OID of the BSONMINN aggregate function */
	Oid ApiCatalogBsonMinNAggregateFunctionOid;

	/* OID of the BSONSTDDEVSAMP aggregate function */
	Oid ApiCatalogBsonStdDevSampAggregateFunctionOid;

	/* OID of the BSONMEDIAN aggregate function */
	Oid ApiCatalogBsonMedianAggregateFunctionOid;

	/* OID of the BSONPERCENTILE aggregate function */
	Oid ApiCatalogBsonPercentileAggregateFunctionOid;

	/* OID of the pg_catalog.any_value aggregate */
	Oid PostgresAnyValueFunctionOid;

	/* OID of the row_get_bson function */
	Oid ApiCatalogRowGetBsonFunctionOid;

	/* OID of the bson_expression_get function */
	Oid ApiCatalogBsonExpressionGetFunctionOid;

	/* OID of the bson_expression_get with let function */
	Oid ApiCatalogBsonExpressionGetWithLetFunctionOid;

	/* OID of the bson_expression_partition_get function */
	Oid ApiCatalogBsonExpressionPartitionGetFunctionOid;

	/* OID of the bson_expression_partition_get with let function */
	Oid ApiCatalogBsonExpressionPartitionGetWithLetFunctionOid;

	/* OID of the bson_expression_map function */
	Oid ApiCatalogBsonExpressionMapFunctionOid;

	/* OID of the bson_expression_map with let arguments function */
	Oid ApiCatalogBsonExpressionMapWithLetFunctionOid;

	/*Oid of the change_stream_aggregation function*/
	Oid ApiCatalogChangeStreamFunctionId;

	/* OID of the pg_catalog.random() function */
	Oid PgRandomFunctionOid;

	/* OID of the bson_dollar_lookup_extract_filter_expression function */
	Oid ApiCatalogBsonLookupExtractFilterExpressionOid;

	/* OID of the bson_dollar_lookup_extract_filter_array function */
	Oid ApiCatalogBsonLookupExtractFilterArrayOid;

	/* OID of the ApiInternalSchemaName.bson_dollar_lookup_extract_filter_expression function */
	Oid DocumentDBInternalBsonLookupExtractFilterExpressionOid;

	/* OID of the bson_const_fill window function */
	Oid BsonConstFillFunctionOid;

	/* OID of ApiInternalSchemaName.bson_dollar_lookup_join_filter function */
	Oid BsonDollarLookupJoinFilterFunctionOid;

	/* OID of the bson_lookup_unwind function */
	Oid BsonLookupUnwindFunctionOid;

	/* OID of the bson_distinct_unwind function */
	Oid BsonDistinctUnwindFunctionOid;

	/* OID of the bson_expression_partition_get function */
	Oid BsonExpressionPartitionByFieldsGetFunctionOid;

	/* Postgis box2df type id */
	Oid Box2dfTypeId;

	/* Postgis geometry type id */
	Oid GeometryTypeId;

	/* Postgis geography type id */
	Oid GeographyTypeId;

	/* Postgis GIDX type id */
	Oid GIDXTypeId;

	/* Postgis geometry array type id */
	Oid GeometryArrayTypeId;

	/* Oid of bson_gist_geography_distance function id */
	Oid BsonGistGeographyDistanceFunctionOid;

	/* Oid of bson_gist_geography_consistent function id */
	Oid BsonGistGeographyConsistentFunctionOid;

	/* Oid of <|-|> geonear distance operator id */
	Oid BsonGeonearDistanceOperatorId;

	/* Oid of @|><| geonear distance range opeartor id */
	Oid BsonGeonearDistanceRangeOperatorId;

	/* Oid of bson_dollar_project_geonear function id*/
	Oid BsonDollarProjectGeonearFunctionOid;

	/* Oid of bson_dollar_geointersects function */
	Oid BsonDollarGeoIntersectsFunctionOid;

	/* Oid of bson_dollar_geowithin function */
	Oid BsonDollarGeowithinFunctionOid;

	/* Oid of bson_extract_geometry function */
	Oid BsonExtractGeometryFunctionId;

	/* Oid of bson_extract_geometry_array function */
	Oid BsonExtractGeometryArrayFunctionId;

	/* Oid of the geometry::geography cast function */
	Oid PostgisGeometryAsGeography;

	/* Oid of the ST_IsValidReason function  */
	Oid PostgisGeometryIsValidDetailFunctionId;

	/* Oid of the ST_makeValid function */
	Oid PostgisGeometryMakeValidFunctionId;

	/* Oid of bson_validate_geometry function */
	Oid BsonValidateGeometryFunctionId;

	/* Oid of bson_validate_geography function */
	Oid BsonValidateGeographyFunctionId;

	/* Oid of Posgtis ST_ForcePolygonCW function */
	Oid PostgisForcePolygonCWFunctionId;

	/* Oid of Postgis ST_AsBinary function */
	Oid PostgisGeometryAsBinaryFunctionId;

	/* Oid of the Postgis GIST support function geometry_gist_compress_2d */
	Oid PostgisGeometryGistCompress2dFunctionId;

	/* Oid of the Postgis GIST support function geography_gist_compress */
	Oid PostgisGeographyGistCompressFunctionId;

	/* Oid of the Postgis GIST support function geometry_gist_consistent_2d */
	Oid PostgisGeometryGistConsistent2dFunctionId;

	/* Oid of the Postgis GIST support function geography_gist_consistent */
	Oid PostgisGeographyGistConsistentFunctionId;

	/* Oid of the Box3d Postgis function */
	Oid PostgisMake3dBoxFunctionId;

	/* Oid of the ST_MakeEnvelope Postgis function */
	Oid PostgisMakeEnvelopeFunctionId;

	/* Oid of the ST_MakePoint Postgis function */
	Oid PostgisMakePointFunctionId;

	/* Oid of the geometry ST_buffer Postgis function */
	Oid PostgisGeometryBufferFunctionId;

	/* Oid of the geography ST_buffer Postgis function */
	Oid PostgisGeographyBufferFunctionId;

	/* Oid of the ST_Collect Postgis function */
	Oid PostgisCollectFunctionId;

	/* Oid of the ST_Area Postgis function */
	Oid PostgisGeometryAreaFunctionId;

	/* Oid of the st_geomfromwkb Postgis function */
	Oid PostgisGeometryFromEWKBFunctionId;

	/* Oid of the ST_MakePolygon Postgis function */
	Oid PostgisMakePolygonFunctionId;

	/* Oid of the ST_MakeLine Postgis function */
	Oid PostgisMakeLineFunctionId;

	/* Oid of the ST_geogfromwkb function */
	Oid PostgisGeographyFromWKBFunctionId;

	/* Oid of ST_Covers (geography) Postgis function */
	Oid PostgisGeographyCoversFunctionId;

	/* Oid of ST_DWithin (geography) Postgis function */
	Oid PostgisGeographyDWithinFunctionId;

	/* Oid of geometry_distance_centroid Postgis function */
	Oid PostgisGeometryDistanceCentroidFunctionId;

	/* Oid of geography_distance_knn Postgis function */
	Oid PostgisGeographyDistanceKNNFunctionId;

	/* Oid of gist distance support function for geometry */
	Oid PostgisGeometryGistDistanceFunctionId;

	/* Oid of gist distance support function for geography */
	Oid PostgisGeographyGistDistanceFunctionId;

	/* Oid of ST_DWithin (geometry) Postgis function */
	Oid PostgisGeometryDWithinFunctionId;

	/* Oid of _ST_EXPAND (geography) Postgis function */
	Oid PostgisGeographyExpandFunctionId;

	/* Oid of ST_EXPAND (geometry) Postgis function */
	Oid PostgisGeometryExpandFunctionId;

	/* Oid of overlaps_2d(box2df, geometry) postgis function */
	Oid PostgisBox2dfGeometryOverlapsFunctionId;

	/* Oid of overlaps_geog(gidx, geography) Postgis function */
	Oid PostgisGIDXGeographyOverlapsFunctionId;

	/* Oid of the ST_Covers (geometry) Postgis function */
	Oid PostgisGeometryCoversFunctionId;

	/* Oid of the ST_Intersects Postgis function for geographies*/
	Oid PostgisGeographyIntersectsFunctionId;

	/* Oid of the ST_Intersects Postgis function for geometries */
	Oid PostgisGeometryIntersectsFunctionId;

	/* Oid of the ST_SetSRID Postgis function */
	Oid PostgisSetSRIDFunctionId;

	/* Oid of the ApiInternalSchemaName.index_build_is_in_progress function */
	Oid IndexBuildIsInProgressFunctionId;

	/* Oid of the ApiDataSchemaName namespace */
	Oid ApiDataNamespaceOid;

	/* OID of the ApiInternalSchemaName.update_worker function */
	Oid UpdateWorkerFunctionOid;

	/* OID of the ApiInternalSchemaName.insert_worker function */
	Oid InsertWorkerFunctionOid;

	/* OID of the ApiInternalSchemaName.delete_worker function */
	Oid DeleteWorkerFunctionOid;

	/* OID of ApiInternalSchemaName.{ExtensionObjectPrefix}_core_bson_to_bson*/
	Oid DocumentDBCoreBsonToBsonFunctionOId;

	/* Oid of array type for bson */
	Oid BsonArrayTypeOid;

	/* OID of the bson index bounds type */
	Oid BsonIndexBoundsTypeOid;

	/* OID of the bsonindexbounds[] type */
	Oid BsonIndexBoundsArrayTypeOid;

	/* Oid of ApiInternalSchemaName.bson_query_match with collation and let */
	Oid BsonQueryMatchWithLetAndCollationFunctionId;
} DocumentDBApiOidCacheData;

static DocumentDBApiOidCacheData Cache;

/*
 * InitializeDocumentDBApiExtensionCache (re)initializes the cache.
 *
 * This function either completes and sets CacheValidity to valid, or throws
 * an OOM and leaves CacheValidity as invalid. In the latter case, any allocated
 * memory will be reset on the next invocation.
 */
void
InitializeDocumentDBApiExtensionCache(void)
{
	if (CacheValidity == CACHE_VALID)
	{
		return;
	}

	/* we create a memory context and register the invalidation handler once */
	if (DocumentDBApiMetadataCacheContext == NULL)
	{
		/* postgres does not always initialize CacheMemoryContext */
		CreateCacheMemoryContext();

		DocumentDBApiMetadataCacheContext = AllocSetContextCreate(CacheMemoryContext,
																  "DocumentDBApiMetadataCacheContext ",
																  ALLOCSET_DEFAULT_SIZES);

		CacheRegisterRelcacheCallback(InvalidateDocumentDBApiCache, (Datum) 0);
	}

	/* reset any previously allocated memory. Code below is sensitive to OOMs */
	MemoryContextReset(DocumentDBApiMetadataCacheContext);

	/* clear the cache data */
	memset(&Cache, 0, sizeof(Cache));

	/*
	 * Check whether the extension exists and is not still be created or
	 * altered.
	 */
	bool missingOK = true;
	Cache.DocumentDBApiExtensionId = get_extension_oid(ApiExtensionName, missingOK);
	if (Cache.DocumentDBApiExtensionId == InvalidOid ||
		(CurrentExtensionObject == Cache.DocumentDBApiExtensionId && creating_extension))
	{
		CacheValidity = CACHE_VALID_NO_EXTENSION;

		return;
	}

	/* since the extension exists, we expect ApiCatalogSchemaName to exist too */
	missingOK = false;
	Cache.MongoCatalogNamespaceId = get_namespace_oid(ApiCatalogSchemaName, missingOK);

	/* look up the ApiCatalogSchemaName.collections OID to catch invalidations */
	Cache.CollectionsTableId = get_relname_relid("collections",
												 Cache.MongoCatalogNamespaceId);

	/* after cache reset (e.g. drop+create extension), also reset collections cache */
	ResetCollectionsCache();

	/* we made it here without out of memory errors */
	CacheValidity = CACHE_VALID;
}


/* Invalidates the collections cache using the collections table oid.
 * this is used to be able to invalidate the cache via the version cache
 * so that the lifetime of both are tight together.
 */
void
InvalidateCollectionsCache()
{
	if (Cache.CollectionsTableId != InvalidOid)
	{
		InvalidateDocumentDBApiCache((Datum) 0, Cache.CollectionsTableId);
	}
}


/*
 * InvalidateDocumentDBApiCache is called when receiving invalidations from other
 * backends.
 *
 * This can happen any time postgres code calls AcceptInvalidationMessages(), e.g
 * after obtaining a relation lock. We remove entries from the cache. They will
 * still be temporarily usable until new entries are added to the cache.
 */
static void
InvalidateDocumentDBApiCache(Datum argument, Oid relationId)
{
	if (relationId == InvalidOid || relationId == Cache.CollectionsTableId)
	{
		/*
		 * Invalidations of ApiCatalogSchemaName.collections typically indicate
		 * CREATE/ALTER/DROP EXTENSION. Reset the whole cache.
		 */
		CacheValidity = CACHE_INVALID;
		ResetCollectionsCache();
		InvalidateVersionCache();
	}
	else
	{
		/* got an invalidation for a specific relation */

		if (CacheValidity == CACHE_VALID)
		{
			/* free the collection cache entry for the given relation */
			InvalidateCollectionByRelationId(relationId);
		}
		else
		{
			/*
			 * If the cache is not valid, we'll reset the collections
			 * cache on the next call to InitializeDocumentDBApiExtensionCache.
			 */
		}
	}
}


/*
 * Helper method abstracting typename parsing across PG Versions
 */
inline static TypeName *
ParseTypeNameCore(const char *typeName)
{
#if PG_VERSION_NUM >= 160000
	return typeStringToTypeName(typeName, NULL);
#else
	return typeStringToTypeName(typeName);
#endif
}


/*
 * IsDocumentDBApiExtensionActive returns whether the current extension exists and is
 * usable (not being altered, no pg_upgrade in progress).
 */
bool
IsDocumentDBApiExtensionActive(void)
{
	InitializeDocumentDBApiExtensionCache();

	return CacheValidity == CACHE_VALID && !IsBinaryUpgrade &&
		   !(creating_extension && CurrentExtensionObject ==
			 Cache.DocumentDBApiExtensionId);
}


/*
 * DocumentDBApiExtensionOwner returns OID of the owner of current extension.
 */
Oid
DocumentDBApiExtensionOwner(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.DocumentDBApiExtensionOwner != InvalidOid)
	{
		return Cache.DocumentDBApiExtensionOwner;
	}

	bool useIndex = true;
	Snapshot scanSnapshot = NULL;

	ScanKeyData scanKey[PG_EXTENSION_NAME_SCAN_NARGS];
	ScanKeyInit(&scanKey[0], Anum_pg_extension_extname, BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(ApiExtensionName));

	Relation relation = table_open(ExtensionRelationId, AccessShareLock);
	SysScanDesc scandesc = systable_beginscan(relation, ExtensionNameIndexId, useIndex,
											  scanSnapshot, PG_EXTENSION_NAME_SCAN_NARGS,
											  scanKey);

	/* there can be at most one matching tuple */
	HeapTuple extensionTuple = systable_getnext(scandesc);
	if (!HeapTupleIsValid(extensionTuple))
	{
		ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("API extension has not been loaded")));
	}

	Form_pg_extension extensionForm = (Form_pg_extension) GETSTRUCT(extensionTuple);
	Cache.DocumentDBApiExtensionOwner = extensionForm->extowner;

	systable_endscan(scandesc);
	table_close(relation, AccessShareLock);

	return Cache.DocumentDBApiExtensionOwner;
}


/*
 * ApiCollectionFunctionId returns the OID of the ApiSchemaName.collection()
 * function.
 */
Oid
ApiCollectionFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CollectionFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiSchemaName),
											makeString("collection"));
		Oid paramOids[2] = { TEXTOID, TEXTOID };
		bool missingOK = false;

		Cache.CollectionFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.CollectionFunctionId;
}


Oid
DocumentDBApiCollectionFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.DocumentDBApiCollectionFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiSchemaNameV2),
											makeString("collection"));
		Oid paramOids[2] = { TEXTOID, TEXTOID };

		/* Allow this to be missing (for compat) */
		bool missingOK = true;

		Cache.DocumentDBApiCollectionFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.DocumentDBApiCollectionFunctionId;
}


/*
 * BigintEqualOperatorId returns the OID of the <bigint> = <bigint> operator.
 */
Oid
BigintEqualOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BigintEqualOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("="));

		Cache.BigintEqualOperatorId =
			OpernameGetOprid(operatorNameList, INT8OID, INT8OID);
	}

	return Cache.BigintEqualOperatorId;
}


Oid
TextEqualOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.TextEqualOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("="));

		Cache.TextEqualOperatorId =
			OpernameGetOprid(operatorNameList, TEXTOID, TEXTOID);
	}

	return Cache.TextEqualOperatorId;
}


Oid
TextNotEqualOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.TextNotEqualOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("<>"));

		Cache.TextNotEqualOperatorId =
			OpernameGetOprid(operatorNameList, TEXTOID, TEXTOID);
	}

	return Cache.TextNotEqualOperatorId;
}


Oid
TextLessOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.TextLessOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("<"));

		Cache.TextLessOperatorId =
			OpernameGetOprid(operatorNameList, TEXTOID, TEXTOID);
	}

	return Cache.TextLessOperatorId;
}


/*
 * ApiCreateIndexesProcedureId returns the OID of the
 * ApiSchema.create_indexes() procedure.
 */
Oid
ApiCreateIndexesProcedureId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CreateIndexesProcedureId == InvalidOid)
	{
		ObjectWithArgs *objectWithArgs = makeNode(ObjectWithArgs);
		objectWithArgs->objname = list_make2(makeString(ApiSchemaName),
											 makeString("create_indexes"));

		objectWithArgs->objargs = list_make4(ParseTypeNameCore("text"),
											 ParseTypeNameCore(FullBsonTypeName),
											 ParseTypeNameCore(FullBsonTypeName),
											 ParseTypeNameCore("boolean"));

		FunctionParameter *inDatabaseNameParam = makeNode(FunctionParameter);
		inDatabaseNameParam->name = "p_database_name";
		inDatabaseNameParam->argType = ParseTypeNameCore("text");
		inDatabaseNameParam->mode = FUNC_PARAM_IN;

		FunctionParameter *inBsonArgParam = makeNode(FunctionParameter);
		inBsonArgParam->name = "p_arg";
		inBsonArgParam->argType = ParseTypeNameCore(FullBsonTypeName);
		inBsonArgParam->mode = FUNC_PARAM_IN;

		FunctionParameter *outBsonResultParam = makeNode(FunctionParameter);
		outBsonResultParam->name = "retval";
		outBsonResultParam->argType = ParseTypeNameCore(FullBsonTypeName);
		outBsonResultParam->mode = FUNC_PARAM_INOUT;

		FunctionParameter *outOkResultParam = makeNode(FunctionParameter);
		outOkResultParam->name = "ok";
		outOkResultParam->argType = ParseTypeNameCore("boolean");
		outOkResultParam->mode = FUNC_PARAM_INOUT;

		objectWithArgs->objfuncargs = list_make4(inDatabaseNameParam, inBsonArgParam,
												 outBsonResultParam,
												 outOkResultParam);

		bool missingOk = false;
		Cache.CreateIndexesProcedureId =
			LookupFuncWithArgs(OBJECT_PROCEDURE, objectWithArgs, missingOk);
	}

	return Cache.CreateIndexesProcedureId;
}


/*
 * ApiReIndexProcedureId returns the OID of the
 * ApiSchema.re_index() procedure.
 */
Oid
ApiReIndexProcedureId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ReindexProcedureId == InvalidOid)
	{
		ObjectWithArgs *objectWithArgs = makeNode(ObjectWithArgs);
		objectWithArgs->objname = list_make2(makeString(ApiSchemaName),
											 makeString("re_index"));

		objectWithArgs->objargs = list_make4(ParseTypeNameCore("text"),
											 ParseTypeNameCore("text"),
											 ParseTypeNameCore(FullBsonTypeName),
											 ParseTypeNameCore("boolean"));

		FunctionParameter *inDatabaseNameParam = makeNode(FunctionParameter);
		inDatabaseNameParam->name = "p_database_name";
		inDatabaseNameParam->argType = ParseTypeNameCore("text");
		inDatabaseNameParam->mode = FUNC_PARAM_IN;

		FunctionParameter *inBsonArgParam = makeNode(FunctionParameter);
		inBsonArgParam->name = "p_collection_name";
		inBsonArgParam->argType = ParseTypeNameCore("text");
		inBsonArgParam->mode = FUNC_PARAM_IN;

		FunctionParameter *outBsonResultParam = makeNode(FunctionParameter);
		outBsonResultParam->name = "retval";
		outBsonResultParam->argType = ParseTypeNameCore(FullBsonTypeName);
		outBsonResultParam->mode = FUNC_PARAM_INOUT;

		FunctionParameter *outOkResultParam = makeNode(FunctionParameter);
		outOkResultParam->name = "ok";
		outOkResultParam->argType = ParseTypeNameCore("boolean");
		outOkResultParam->mode = FUNC_PARAM_INOUT;

		objectWithArgs->objfuncargs = list_make4(inDatabaseNameParam, inBsonArgParam,
												 outBsonResultParam,
												 outOkResultParam);

		bool missingOk = false;
		Cache.ReindexProcedureId =
			LookupFuncWithArgs(OBJECT_PROCEDURE, objectWithArgs, missingOk);
	}

	return Cache.ReindexProcedureId;
}


/*
 * BsonQueryMatchFunctionId returns the OID of ApiCatalogSchemaName.bson_query_match function.
 */
Oid
BsonQueryMatchFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonQueryMatchFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString("bson_query_match"));
		Oid bsonTypeId = BsonTypeId();
		Oid paramOids[2] = { bsonTypeId, bsonTypeId };
		bool missingOK = false;

		Cache.BsonQueryMatchFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.BsonQueryMatchFunctionId;
}


/*
 * BsonQueryMatchWithLetAndCollationFunctionId returns the OID of ApiCatalogSchemaName.bson_query_match function
 * with collation and let arguments.
 */
Oid
BsonQueryMatchWithLetAndCollationFunctionId(void)
{
	int nargs = 4;
	Oid bsonTypeId = BsonTypeId();
	Oid argTypes[4] = { bsonTypeId, bsonTypeId, bsonTypeId, TEXTOID };
	bool missingOk = true;

	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonQueryMatchWithLetAndCollationFunctionId,
		DocumentDBApiInternalSchemaName,
		"bson_query_match", nargs,
		argTypes, missingOk);
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_eq function.
 */
Oid
BsonEqualMatchRuntimeFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonEqualMatchRuntimeFunctionId,
									   "bson_dollar_eq", BsonTypeId(),
									   GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_eq Runtime operator #=.
 */
Oid
BsonEqualMatchRuntimeOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonEqualMatchRuntimeOperatorId,
							   BsonTypeId(), "#=", GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_eq function for index.
 */
Oid
BsonEqualMatchIndexFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonEqualMatchIndexFunctionId,
									   "bson_dollar_eq", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_gt function.
 */
Oid
BsonGreaterThanMatchRuntimeFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonGreaterThanMatchRuntimeFunctionId,
									   "bson_dollar_gt", BsonTypeId(),
									   GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_gt Runtime operator #>.
 */
Oid
BsonGreaterThanMatchRuntimeOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonGreaterThanMatchRuntimeOperatorId,
							   BsonTypeId(), "#>", GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_gt function for index.
 */
Oid
BsonGreaterThanMatchIndexFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonGreaterThanMatchIndexFunctionId,
									   "bson_dollar_gt", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_gte function.
 */
Oid
BsonGreaterThanEqualMatchRuntimeFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonGreaterThanEqualMatchRuntimeFunctionId,
									   "bson_dollar_gte", BsonTypeId(),
									   GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_gte Runtime operator #>=.
 */
Oid
BsonGreaterThanEqualMatchRuntimeOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonGreaterThanEqualMatchRuntimeOperatorId,
							   BsonTypeId(), "#>=", GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_gte function for index.
 */
Oid
BsonGreaterThanEqualMatchIndexFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonGreaterThanEqualMatchIndexFunctionId,
									   "bson_dollar_gte", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_lt function.
 */
Oid
BsonLessThanMatchRuntimeFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonLessThanMatchRuntimeFunctionId,
									   "bson_dollar_lt", BsonTypeId(),
									   GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_lt Runtime operator #<.
 */
Oid
BsonLessThanMatchRuntimeOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonLessThanMatchRuntimeOperatorId,
							   BsonTypeId(), "#<", GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_lt function.
 */
Oid
BsonLessThanMatchIndexFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonLessThanMatchIndexFunctionId,
									   "bson_dollar_lt", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_lte function.
 */
Oid
BsonLessThanEqualMatchRuntimeFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonLessThanEqualMatchRuntimeFunctionId,
									   "bson_dollar_lte", BsonTypeId(),
									   GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_lte Runtime operator #<=.
 */
Oid
BsonLessThanEqualMatchRuntimeOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonLessThanEqualMatchRuntimeOperatorId,
							   BsonTypeId(), "#<=", GetClusterBsonQueryTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_lte function for index.
 */
Oid
BsonLessThanEqualMatchIndexFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonLessThanEqualMatchIndexFunctionId,
									   "bson_dollar_lte", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_range function.
 */
Oid
BsonRangeMatchFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(&Cache.BsonRangeMatchFunctionId,
										ApiCatalogToApiInternalSchemaName,
										"bson_dollar_range", nargs, argTypes,
										missingOk);
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_not_lte function.
 */
Oid
BsonNotLessThanEqualFunctionId(void)
{
	bool missingOk = true;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.BsonNotLessThanEqualFunctionId,
		"bson_dollar_not_lte",
		BsonTypeId(),
		BsonTypeId(), missingOk);
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_not_lt function.
 */
Oid
BsonNotLessThanFunctionId(void)
{
	bool missingOk = true;
	return GetDocumentDBInternalBinaryOperatorFunctionId(&Cache.BsonNotLessThanFunctionId,
														 "bson_dollar_not_lt",
														 BsonTypeId(),
														 BsonTypeId(), missingOk);
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_not_gt function.
 */
Oid
BsonNotGreaterThanFunctionId(void)
{
	bool missingOk = true;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.BsonNotGreaterThanFunctionId,
		"bson_dollar_not_gt",
		BsonTypeId(),
		BsonTypeId(), missingOk);
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_not_gte function.
 */
Oid
BsonNotGreaterThanEqualFunctionId(void)
{
	bool missingOk = true;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.BsonNotGreaterThanEqualFunctionId,
		"bson_dollar_not_gte", BsonTypeId(),
		BsonTypeId(), missingOk);
}


/*
 * Returns the OID of  ApiCatalogSchemaName.<|-|> geoNear distance operator
 */
Oid
BsonGeonearDistanceOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonGeonearDistanceOperatorId,
							   BsonTypeId(), "<|-|>", BsonTypeId());
}


/*
 * Returns the OID of  ApiInternalSchemaName.@|><| geoNear distance range operator
 */
Oid
BsonGeonearDistanceRangeOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonGeonearDistanceRangeOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("@|><|"));

		Cache.BsonGeonearDistanceRangeOperatorId =
			OpernameGetOprid(operatorNameList, BsonTypeId(), BsonTypeId());
	}

	return Cache.BsonGeonearDistanceRangeOperatorId;
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_range Runtime operator #<>.
 */
Oid
BsonRangeMatchOperatorOid(void)
{
	return GetBinaryOperatorId(&Cache.BsonRangeMatchOperatorOid,
							   BsonTypeId(), "@<>", BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_in function.
 */
Oid
BsonInMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonInMatchFunctionId,
									   "bson_dollar_in", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_nin function.
 */
Oid
BsonNinMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonNinMatchFunctionId,
									   "bson_dollar_nin", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_ne function.
 */
Oid
BsonNotEqualMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonNotEqualMatchFunctionId,
									   "bson_dollar_ne", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_all function.
 */
Oid
BsonAllMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonAllMatchFunctionId,
									   "bson_dollar_all", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_elemmatch function.
 */
Oid
BsonElemMatchMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonElemMatchMatchFunctionId,
									   "bson_dollar_elemmatch", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_bits_all_clear function.
 */
Oid
BsonBitsAllClearFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonBitsAllClearFunctionId,
									   "bson_dollar_bits_all_clear", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_bits_all_clear function.
 */
Oid
BsonBitsAnyClearFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonBitsAnyClearFunctionId,
									   "bson_dollar_bits_any_clear", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_bits_all_set function.
 */
Oid
BsonBitsAllSetFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonBitsAllSetFunctionId,
									   "bson_dollar_bits_all_set", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_bits_any_set function.
 */
Oid
BsonBitsAnySetFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonBitsAnySetFunctionId,
									   "bson_dollar_bits_any_set", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_expr function.
 */
Oid
BsonExprFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonExprFunctionId,
									   "bson_dollar_expr", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_json_schema function.
 */
Oid
BsonJsonSchemaFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonJsonSchemaFunctionId,
									   "bson_dollar_json_schema", BsonTypeId(),
									   BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_expr function.
 */
Oid
BsonExprWithLetFunctionId(void)
{
	return GetOperatorFunctionIdThreeArgs(&Cache.BsonExprWithLetFunctionId,
										  DocumentDBApiInternalSchemaName,
										  "bson_dollar_expr", DocumentDBCoreBsonTypeId(),
										  DocumentDBCoreBsonTypeId(),
										  DocumentDBCoreBsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_text function.
 */
Oid
BsonTextFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(
		&Cache.BsonTextFunctionId,
		"bson_dollar_text",
		BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_regex function.
 */
Oid
BsonRegexMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonRegexMatchFunctionId,
									   "bson_dollar_regex", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_mod function.
 */
Oid
BsonModMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonModMatchFunctionId,
									   "bson_dollar_mod", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_size function.
 */
Oid
BsonSizeMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonSizeMatchFunctionId,
									   "bson_dollar_size", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_type function.
 */
Oid
BsonTypeMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonTypeMatchFunctionId,
									   "bson_dollar_type", BsonTypeId(), BsonTypeId());
}


/*
 * Returns the OID of ApiCatalogSchemaName.bson_dollar_exists function.
 */
Oid
BsonExistsMatchFunctionId(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonExistsMatchFunctionId,
									   "bson_dollar_exists", BsonTypeId(), BsonTypeId());
}


/*
 * BsonEqualMatchOperatorId returns the OID of the <bson> @= <bson> operator.
 */
Oid
BsonEqualMatchOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonEqualMatchOperatorId,
							   BsonTypeId(), "@=", BsonTypeId());
}


Oid
BsonInOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonInOperatorId,
							   BsonTypeId(), "@*=", BsonTypeId());
}


/*
 * BsonEqualOperatorId returns the OID of the <bson> = <bson> operator.
 */
Oid
BsonEqualOperatorId(void)
{
	return GetCoreBinaryOperatorId(&Cache.BsonEqualOperatorId,
								   BsonTypeId(), "=", BsonTypeId());
}


/*
 * BsonQueryOperatorId returns the OID of the <bson> @@ <bson> operator.
 */
Oid
BsonQueryOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.BsonQueryOperatorId,
							   BsonTypeId(), "@@", BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $eq <bson> function.
 */
Oid
BsonValueEqualMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueEqualMatchFunctionId,
											   "bson_value_dollar_eq", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson> ##= <bsonindexbounds> operator.
 */
Oid
BsonIndexBoundsEqualOperatorId(void)
{
	return GetInternalBinaryOperatorId(
		&Cache.BsonIndexBoundsEqualOperatorId,
		BsonTypeId(), "##=", BsonIndexBoundsTypeId());
}


Oid
BsonIndexBoundsEqualOperatorFuncId(void)
{
	return GetBinaryOperatorFunctionIdWithSchema(
		&Cache.BsonIndedBoundsEqualOperatorFuncId,
		"bson_dollar_eq", BsonTypeId(), BsonIndexBoundsTypeId(), ApiInternalSchemaNameV2);
}


/*
 * Returns the OID of the <bson_value_t> $gt <bson> function.
 */
Oid
BsonValueGreaterThanMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueGreaterMatchFunctionId,
											   "bson_value_dollar_gt", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $gte <bson> function.
 */
Oid
BsonValueGreaterThanEqualMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(
		&Cache.BsonValueGreaterEqualMatchFunctionId,
		"bson_value_dollar_gte", INTERNALOID,
		BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $lt <bson> function.
 */
Oid
BsonValueLessThanMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueLessMatchFunctionId,
											   "bson_value_dollar_lt", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $lte <bson> function.
 */
Oid
BsonValueLessThanEqualMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueLessEqualMatchFunctionId,
											   "bson_value_dollar_lte", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $size <bson> function.
 */
Oid
BsonValueSizeMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueSizeMatchFunctionId,
											   "bson_value_dollar_size", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $type <bson> function.
 */
Oid
BsonValueTypeMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueTypeMatchFunctionId,
											   "bson_value_dollar_type", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $in <bson> function.
 */
Oid
BsonValueInMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueInMatchFunctionId,
											   "bson_value_dollar_in", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $nin <bson> function.
 */
Oid
BsonValueNinMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueNinMatchFunctionId,
											   "bson_value_dollar_nin", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $ne <bson> function.
 */
Oid
BsonValueNotEqualMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueNotEqualMatchFunctionId,
											   "bson_value_dollar_ne", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $exists <bson> function.
 */
Oid
BsonValueExistsMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueExistsMatchFunctionId,
											   "bson_value_dollar_exists", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $elemMatch <bson> function.
 */
Oid
BsonValueElemMatchMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueElemMatchMatchFunctionId,
											   "bson_value_dollar_elemmatch", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $all <bson> function.
 */
Oid
BsonValueAllMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueAllMatchFunctionId,
											   "bson_value_dollar_all", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $regex <bson> function.
 */
Oid
BsonValueRegexMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueRegexMatchFunctionId,
											   "bson_value_dollar_regex", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $mod <bson> function.
 */
Oid
BsonValueModMatchFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueModMatchFunctionId,
											   "bson_value_dollar_mod", INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $bitsAllClear <bson> function.
 */
Oid
BsonValueBitsAllClearFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueBitsAllClearFunctionId,
											   "bson_value_dollar_bits_all_clear",
											   INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $bitsAnyClear <bson> function.
 */
Oid
BsonValueBitsAnyClearFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueBitsAnyClearFunctionId,
											   "bson_value_dollar_bits_any_clear",
											   INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $bitsAllClear <bson> function.
 */
Oid
BsonValueBitsAllSetFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueBitsAllSetFunctionId,
											   "bson_value_dollar_bits_all_set",
											   INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the <bson_value_t> $bitsAnyClear <bson> function.
 */
Oid
BsonValueBitsAnySetFunctionId(void)
{
	return GetInternalBinaryOperatorFunctionId(&Cache.BsonValueBitsAnySetFunctionId,
											   "bson_value_dollar_bits_any_set",
											   INTERNALOID,
											   BsonTypeId());
}


/*
 * Returns the OID of the "drandom" internal postgres method
 */
Oid
PostgresDrandomFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresDrandomFunctionId, "drandom");
}


/*
 * Returns the OID of the "float8_timestamptz" internal postgres method
 */
Oid
PostgresToTimestamptzFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresToTimestamptzFunctionId,
										 "float8_timestamptz");
}


/*
 * Returns the OID of the "date_part" internal postgres method
 */
Oid
PostgresDatePartFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresDatePartFunctionId,
										 "timestamp_part");
}


/*
 * Returns the OID of the "timestamptz_zone" internal postgres method
 */
Oid
PostgresTimestampToZoneFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresTimestampToZoneFunctionId,
										 "timestamptz_zone");
}


/*
 * Returns the OID of the int4 + int4 function
 */
Oid
PostgresInt4PlusFunctionOid(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresInt4PlusFunctionOid,
										 "int4pl");
}


/*
 * Returns the OID of the "make_interval" internal postgres method
 */
Oid
PostgresMakeIntervalFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresMakeIntervalFunctionId,
										 "make_interval");
}


/*
 * Returns the OID of the int4 < int4 Function
 */
Oid
PostgresInt4LessOperatorOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PostgresInt4LessOperatorOid == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("<"));

		Cache.PostgresInt4LessOperatorOid =
			OpernameGetOprid(operatorNameList, INT4OID, INT4OID);
	}

	return Cache.PostgresInt4LessOperatorOid;
}


Oid
PostgresInt4LessOperatorFunctionOid(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresInt4LessOperatorFunctionOid,
										 "int4lt");
}


/*
 * Returns the OID of the "timestamp_pl_interval" internal postgres method
 */
Oid
PostgresAddIntervalToTimestampFunctionId(void)
{
	return GetPostgresInternalFunctionId(
		&Cache.PostgresAddIntervalToTimestampFunctionId,
		"timestamp_pl_interval");
}


/*
 * Returns the OID of the "date_pl_interval" internal postgres method
 */
Oid
PostgresAddIntervalToDateFunctionId(void)
{
	return GetPostgresInternalFunctionId(
		&Cache.PostgresAddIntervalToDateFunctionId,
		"date_pl_interval");
}


/*
 * Returns the OID of the int4 = int4 Function
 */
Oid
PostgresInt4EqualOperatorOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PostgresInt4EqualOperatorOid == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("="));

		Cache.PostgresInt4EqualOperatorOid =
			OpernameGetOprid(operatorNameList, INT4OID, INT4OID);
	}

	return Cache.PostgresInt4EqualOperatorOid;
}


/*
 * Returns the OID of the "timestamp_zone" internal postgres method
 */
Oid
PostgresTimestampToZoneWithoutTzFunctionId(void)
{
	return GetPostgresInternalFunctionId(
		&Cache.PostgresTimestampToZoneWithoutTzFunctionId,
		"timestamp_zone");
}


/*
 * Returns the OID of the "to_date" internal postgres method
 */
Oid
PostgresToDateFunctionId(void)
{
	return GetPostgresInternalFunctionId(
		&Cache.PostgresToDateFunctionId,
		"to_date");
}


/*
 * Returns the OID of float8 = float8 operator
 */
Oid
Float8EqualOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Float8EqualOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("="));

		Cache.Float8EqualOperatorId =
			OpernameGetOprid(operatorNameList, FLOAT8OID, FLOAT8OID);
	}

	return Cache.Float8EqualOperatorId;
}


/*
 * Returns the OID of float8 <= float8 operator
 */
Oid
Float8LessThanEqualOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Float8LessThanEqualOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString("<="));

		Cache.Float8LessThanEqualOperatorId =
			OpernameGetOprid(operatorNameList, FLOAT8OID, FLOAT8OID);
	}

	return Cache.Float8LessThanEqualOperatorId;
}


/*
 * Returns the OID of float8 >= float8 operator
 */
Oid
Float8GreaterThanEqualOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Float8GreaterThanEqualOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"),
											makeString(">="));

		Cache.Float8GreaterThanEqualOperatorId =
			OpernameGetOprid(operatorNameList, FLOAT8OID, FLOAT8OID);
	}

	return Cache.Float8GreaterThanEqualOperatorId;
}


/*
 * returns the OID of the "array_append" Postgres function
 */
Oid
PostgresArrayAppendFunctionOid(void)
{
	return GetPostgresInternalFunctionId(
		&Cache.PostgresArrayAppendFunctionOid,
		"array_append");
}


/*
 * Returns the OID of the "timestamptz_bin" internal postgres method
 */
Oid
PostgresDateBinFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresDateBinFunctionId,
										 "timestamptz_bin");
}


/*
 * Returns the OID of the "timestamp_age" internal postgres method
 */
Oid
PostgresAgeBetweenTimestamp(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresAgeBetweenTimestamp,
										 "timestamp_age");
}


/*
 * Returns the OID of the "interval_part" internal postgres method
 */
Oid
PostgresDatePartFromInterval(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresDatePartFromInterval,
										 "interval_part");
}


/*
 * Returns the OID of the "uuid_in" internal postgres method
 */
Oid
PostgresUUIDInFunctionId(void)
{
	return GetPostgresInternalFunctionId(&Cache.PostgresUUIDInFunctionIdOid, "uuid_in");
}


/* Returns the OID of Rum Index Access method.
 */
Oid
RumIndexAmId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.RumIndexAmId == InvalidOid)
	{
		const char *extensionRumAccess = psprintf("%s_rum", ExtensionObjectPrefix);
		HeapTuple tuple = SearchSysCache1(AMNAME, CStringGetDatum(extensionRumAccess));
		if (!HeapTupleIsValid(tuple))
		{
			/*
			 * YB: Rum index is not supported.
			*/
			if (IsYugaByteEnabled())
				return InvalidOid;

			ereport(ERROR,
					(errmsg("Access method \"%s\" not supported.", extensionRumAccess)));
		}
		Form_pg_am accessMethodForm = (Form_pg_am) GETSTRUCT(tuple);
		Cache.RumIndexAmId = accessMethodForm->oid;
		ReleaseSysCache(tuple);
	}

	return Cache.RumIndexAmId;
}


/* Returns the OID of vector ivfflat Index Access method.
 */
Oid
PgVectorIvfFlatIndexAmId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PgVectorIvfFlatIndexAmId == InvalidOid)
	{
		HeapTuple tuple = SearchSysCache1(AMNAME, CStringGetDatum("ivfflat"));
		if (!HeapTupleIsValid(tuple))
		{
			ereport(NOTICE,
					(errmsg(
						 "Access method \"ivfflat\" not supported.")));
		}
		Form_pg_am accessMethodForm = (Form_pg_am) GETSTRUCT(tuple);
		Cache.PgVectorIvfFlatIndexAmId = accessMethodForm->oid;
		ReleaseSysCache(tuple);
	}

	return Cache.PgVectorIvfFlatIndexAmId;
}


/* Returns the OID of vector hnsw Index Access method.
 */
Oid
PgVectorHNSWIndexAmId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PgVectorHNSWIndexAmId == InvalidOid)
	{
		HeapTuple tuple = SearchSysCache1(AMNAME, CStringGetDatum("hnsw"));
		if (!HeapTupleIsValid(tuple))
		{
			ereport(NOTICE,
					(errmsg(
						 "Access method \"hnsw\" not supported.")));
		}
		Form_pg_am accessMethodForm = (Form_pg_am) GETSTRUCT(tuple);
		Cache.PgVectorHNSWIndexAmId = accessMethodForm->oid;
		ReleaseSysCache(tuple);
	}

	return Cache.PgVectorHNSWIndexAmId;
}


/*
 * Returns the function Oid for converting a double[] to a vector
 * specifically the array_to_vector function.
 */
Oid
PgDoubleToVectorFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PgDoubleToVectorFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("public"),
											makeString("array_to_vector"));

		Oid paramOids[3] = { FLOAT8ARRAYOID, INT4OID, BOOLOID };
		bool missingOK = false;
		Cache.PgDoubleToVectorFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.PgDoubleToVectorFunctionOid;
}


/*
 * VectorAsVectorFunctionOid returns the OID of the vector as vector cast function.
 */
Oid
VectorAsVectorFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorAsVectorFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("public"),
											makeString("vector"));

		Oid paramOids[3] = { VectorTypeId(), INT4OID, BOOLOID };
		bool missingOK = false;
		Cache.VectorAsVectorFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.VectorAsVectorFunctionOid;
}


/*
 * BsonTrueFunctionId returns the OID of the bson_true_match function.
 */
Oid
BsonTrueFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonTrueFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString("bson_true_match"));
		Oid paramOids[1] = { BsonTypeId() };
		bool missingOK = false;

		Cache.BsonTrueFunctionId =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.BsonTrueFunctionId;
}


Oid
BsonDensifyRangeWindowFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(&Cache.BsonDensifyRangeWindowFunctionOid,
										DocumentDBApiInternalSchemaName,
										"bson_densify_range", nargs, argTypes,
										missingOk);
}


Oid
BsonDensifyPartitionWindowFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(&Cache.BsonDensifyPartitionWindowFunctionOid,
										DocumentDBApiInternalSchemaName,
										"bson_densify_partition", nargs, argTypes,
										missingOk);
}


Oid
BsonDensifyFullWindowFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(&Cache.BsonDensifyFullWindowFunctionOid,
										DocumentDBApiInternalSchemaName,
										"bson_densify_full", nargs, argTypes,
										missingOk);
}


/*
 * Returns the OID of the ApiSchema.cursor_state function.
 */
Oid
ApiCursorStateFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CursorStateFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiToApiInternalSchemaName),
											makeString("cursor_state"));
		Oid paramOids[2] = { BsonTypeId(), BsonTypeId() };
		bool missingOK = false;

		Cache.CursorStateFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.CursorStateFunctionId;
}


Oid
UpdateWorkerFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.UpdateWorkerFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("update_worker"));
		Oid paramOids[6] = {
			INT8OID, INT8OID, REGCLASSOID, DocumentDBCoreBsonTypeId(),
			DocumentDBCoreBsonSequenceTypeId(), TEXTOID
		};
		bool missingOK = true;

		Cache.UpdateWorkerFunctionOid =
			LookupFuncName(functionNameList, 6, paramOids, missingOK);
	}

	return Cache.UpdateWorkerFunctionOid;
}


Oid
InsertWorkerFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.InsertWorkerFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("insert_worker"));
		Oid paramOids[6] = {
			INT8OID, INT8OID, REGCLASSOID, DocumentDBCoreBsonTypeId(),
			DocumentDBCoreBsonSequenceTypeId(), TEXTOID
		};
		bool missingOK = true;

		Cache.InsertWorkerFunctionOid =
			LookupFuncName(functionNameList, 6, paramOids, missingOK);
	}

	return Cache.InsertWorkerFunctionOid;
}


Oid
DeleteWorkerFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.DeleteWorkerFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("delete_worker"));
		Oid paramOids[6] = {
			INT8OID, INT8OID, REGCLASSOID, DocumentDBCoreBsonTypeId(),
			DocumentDBCoreBsonSequenceTypeId(), TEXTOID
		};
		bool missingOK = true;

		Cache.DeleteWorkerFunctionOid =
			LookupFuncName(functionNameList, 6, paramOids, missingOK);
	}

	return Cache.DeleteWorkerFunctionOid;
}


/*
 * Returns the OID of the ApiSchema.current_cursor_state function.
 */
Oid
ApiCurrentCursorStateFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CurrentCursorStateFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiToApiInternalSchemaName),
											makeString("current_cursor_state"));
		Oid paramOids[1] = { BsonTypeId() };
		bool missingOK = false;

		Cache.CurrentCursorStateFunctionId =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.CurrentCursorStateFunctionId;
}


/*
 * BsonEmptyDataTableFunctionId returns the OID of the empty_data_table function.
 */
Oid
BsonEmptyDataTableFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonEmptyDataTableFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiToApiInternalSchemaName),
											makeString("empty_data_table"));
		Oid paramOids[0] = { };
		bool missingOK = false;

		Cache.BsonEmptyDataTableFunctionId =
			LookupFuncName(functionNameList, 0, paramOids, missingOK);
	}

	return Cache.BsonEmptyDataTableFunctionId;
}


/*
 * ApiCollStatsAggregationFunctionOid returns the OID of the coll_stats_aggregation function.
 */
Oid
ApiCollStatsAggregationFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CollStatsAggregationFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiToApiInternalSchemaName),
											makeString("coll_stats_aggregation"));
		Oid paramOids[3] = { TEXTOID, TEXTOID, BsonTypeId() };
		bool missingOK = false;

		Cache.CollStatsAggregationFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.CollStatsAggregationFunctionOid;
}


/*
 * ApiChangeStreamAggregationFunctionOid returns the OID of the change_stream_aggregation function.
 */
Oid
ApiChangeStreamAggregationFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogChangeStreamFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiInternalSchemaName),
											makeString("change_stream_aggregation"));
		Oid paramOids[4] = { TEXTOID, TEXTOID, BsonTypeId(), BsonTypeId() };
		bool missingOK = false;

		Cache.ApiCatalogChangeStreamFunctionId =
			LookupFuncName(functionNameList, 4, paramOids, missingOK);
	}

	return Cache.ApiCatalogChangeStreamFunctionId;
}


/*
 * ApiIndexStatsAggregationFunctionOid returns the OID of the index_stats_aggregation function.
 */
Oid
ApiIndexStatsAggregationFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.IndexStatsAggregationFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiToApiInternalSchemaName),
											makeString("index_stats_aggregation"));
		Oid paramOids[2] = { TEXTOID, TEXTOID };
		bool missingOK = false;

		Cache.IndexStatsAggregationFunctionOid =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.IndexStatsAggregationFunctionOid;
}


Oid
BsonCurrentOpAggregationFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonCurrentOpAggregationFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiToApiInternalSchemaName),
											makeString("current_op_aggregation"));
		Oid paramOids[1] = { BsonTypeId() };
		bool missingOK = false;

		Cache.BsonCurrentOpAggregationFunctionId =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.BsonCurrentOpAggregationFunctionId;
}


/*
 * IndexSpecAsBsonFunctionId returns the OID of the ApiInternalSchemaName.index_spec_as_bson function.
 */
Oid
IndexSpecAsBsonFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.IndexSpecAsBsonFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiInternalSchemaName),
											makeString("index_spec_as_bson"));
		Oid paramOids[3] = { IndexSpecTypeId(), BOOLOID, TEXTOID };
		bool missingOK = false;

		Cache.IndexSpecAsBsonFunctionId =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.IndexSpecAsBsonFunctionId;
}


/*
 * ExtensionTableSampleSystemRowsFunctionId returns the OID of the tsm system_rows function.
 */
Oid
ExtensionTableSampleSystemRowsFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ExtensionTableSampleSystemRowsFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("public"),
											makeString("system_rows"));
		Oid paramOids[1] = { INTERNALOID };
		bool missingOK = false;

		Cache.ExtensionTableSampleSystemRowsFunctionId =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.ExtensionTableSampleSystemRowsFunctionId;
}


Oid
BsonInRangeIntervalFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonInRangeIntervalFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogToCoreSchemaName),
											makeString("bson_in_range_interval"));
		Oid paramOids[5] = { BsonTypeId(), BsonTypeId(), INTERVALOID, BOOLOID, BOOLOID };
		bool missingOK = false;

		Cache.BsonInRangeIntervalFunctionId =
			LookupFuncName(functionNameList, 5, paramOids, missingOK);
	}

	return Cache.BsonInRangeIntervalFunctionId;
}


Oid
BsonInRangeNumericFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonInRangeNumericFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogToCoreSchemaName),
											makeString("bson_in_range_numeric"));
		Oid paramOids[5] = { BsonTypeId(), BsonTypeId(), BsonTypeId(), BOOLOID, BOOLOID };
		bool missingOK = false;

		Cache.BsonInRangeNumericFunctionId =
			LookupFuncName(functionNameList, 5, paramOids, missingOK);
	}

	return Cache.BsonInRangeNumericFunctionId;
}


Oid
ApiCatalogAggregationPipelineFunctionId(void)
{
	return GetBinaryOperatorFunctionIdMissingOk(
		&Cache.ApiCatalogAggregationPipelineFunctionId,
		"bson_aggregation_pipeline",
		TEXTOID, BsonTypeId(),
		"1.7");
}


Oid
ApiCatalogAggregationFindFunctionId(void)
{
	return GetBinaryOperatorFunctionIdMissingOk(
		&Cache.ApiCatalogAggregationFindFunctionId,
		"bson_aggregation_find",
		TEXTOID, BsonTypeId(),
		"1.7");
}


Oid
ApiCatalogAggregationCountFunctionId(void)
{
	return GetBinaryOperatorFunctionIdMissingOk(
		&Cache.ApiCatalogAggregationCountFunctionId,
		"bson_aggregation_count",
		TEXTOID, BsonTypeId(),
		"1.7");
}


Oid
ApiCatalogAggregationDistinctFunctionId(void)
{
	return GetBinaryOperatorFunctionIdMissingOk(
		&Cache.ApiCatalogAggregationDistinctFunctionId,
		"bson_aggregation_distinct",
		TEXTOID, BsonTypeId(),
		"1.7");
}


Oid
BsonDollarAddFieldsFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.ApiCatalogBsonDollarAddFieldsFunctionOid,
									   "bson_dollar_add_fields", BsonTypeId(),
									   BsonTypeId());
}


Oid
BsonDollarAddFieldsWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiCatalogBsonDollarAddFieldsWithLetFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_dollar_add_fields",
		DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId());
}


Oid
BsonDollarAddFieldsWithLetAndCollationFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonDollarAddFieldsWithLetAndCollationFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_dollar_add_fields",
		DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(), TEXTOID);
}


Oid
BsonDollaMergeDocumentsFunctionOid(void)
{
	bool missingOk = false;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.ApiInternalSchemaBsonDollarMergeDocumentsFunctionOid,
		"bson_dollar_merge_documents",
		BsonTypeId(),
		BsonTypeId(), missingOk);
}


Oid
BsonDollarMergeDocumentAtPathFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiInternalSchemaBsonDollarMergeDocumentAtPathFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_merge_documents_at_path",
		BsonTypeId(),
		BsonTypeId(), TEXTOID);
}


Oid
BsonDollarProjectFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.ApiCatalogBsonDollarProjectFunctionOid,
									   "bson_dollar_project", BsonTypeId(), BsonTypeId());
}


Oid
BsonDollarProjectWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiCatalogBsonDollarProjectWithLetFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_dollar_project",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId());
}


Oid
BsonDollarProjectWithLetAndCollationFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonDollarProjectWithLetAndCollationFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_dollar_project",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId(), TEXTOID);
}


Oid
BsonDollarRedactWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiInternalBsonDollarRedactWithLetFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_redact",
		BsonTypeId(), BsonTypeId(),
		TEXTOID, BsonTypeId());
}


Oid
BsonDollarRedactWithLetAndCollationFunctionOid(void)
{
	return GetOperatorFunctionIdFiveArgs(
		&Cache.ApiInternalBsonDollarRedactWithLetAndCollationFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_redact",
		BsonTypeId(), BsonTypeId(),
		TEXTOID, BsonTypeId(), TEXTOID);
}


Oid
BsonDollarLookupExpressionEvalMergeOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiCatalogBsonDollarLookupExpressionEvalMergeOid,
		DocumentDBApiInternalSchemaName, "bson_dollar_lookup_expression_eval_merge",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId());
}


/*
 * Returns the OID of the bson_dollar_inverse_match function.
 */
Oid
BsonDollarInverseMatchFunctionId()
{
	int nargs = 2;
	Oid argTypes[2] = { DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId() };
	bool missingOk = true;

	Oid result = GetSchemaFunctionIdWithNargs(
		&Cache.ApiCatalogBsonDollarInverseMatchFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_inverse_match", nargs, argTypes,
		missingOk);

	if (result == InvalidOid)
	{
		/* we don't have the function in ApiInternalSchemaName yet, check ApiCatalogSchemaName */
		missingOk = false;
		result = GetSchemaFunctionIdWithNargs(
			&Cache.ApiCatalogBsonDollarInverseMatchFunctionOid,
			ApiCatalogSchemaNameV2,
			"bson_dollar_inverse_match", nargs, argTypes,
			missingOk);
	}

	return result;
}


Oid
BsonDollarProjectFindFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogBsonDollarProjectFindFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString(
												"bson_dollar_project_find"));
		Oid paramOids[3] = { BsonTypeId(), BsonTypeId(), BsonTypeId() };
		bool missingOK = false;

		Cache.ApiCatalogBsonDollarProjectFindFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.ApiCatalogBsonDollarProjectFindFunctionOid;
}


Oid
BsonDollarProjectFindWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonDollarProjectFindWithLetFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_project_find",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId());
}


Oid
BsonDollarProjectFindWithLetAndCollationFunctionOid(void)
{
	return GetOperatorFunctionIdFiveArgs(
		&Cache.ApiCatalogBsonDollarProjectFindWithLetAndCollationFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_project_find",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId(),
		TEXTOID);
}


Oid
BsonDollarMergeHandleWhenMatchedFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiInternalBsonDollarMergeHandleWhenMatchedFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString(
												"bson_dollar_merge_handle_when_matched"));
		Oid *paramOids;
		int nargs = 3;
		if (IsClusterVersionAtleast(DocDB_V0, 102, 0))
		{
			paramOids = (Oid *) palloc(sizeof(Oid) * 5);
			paramOids[0] = BsonTypeId();
			paramOids[1] = BsonTypeId();
			paramOids[2] = INT4OID;
			paramOids[3] = BsonTypeId();
			paramOids[4] = INT4OID;
			nargs = 5;
		}
		else
		{
			paramOids = (Oid *) palloc(sizeof(Oid) * 3);
			paramOids[0] = BsonTypeId();
			paramOids[1] = BsonTypeId();
			paramOids[2] = INT4OID;
		}

		bool missingOK = false;

		Cache.ApiInternalBsonDollarMergeHandleWhenMatchedFunctionId =
			LookupFuncName(functionNameList, nargs, paramOids, missingOK);
	}

	return Cache.ApiInternalBsonDollarMergeHandleWhenMatchedFunctionId;
}


Oid
BsonDollarMergeAddObjectIdFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiInternalBsonDollarMergeAddObjectIdFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString(
												"bson_dollar_merge_add_object_id"));
		Oid *paramOids;
		int nargs = 2;
		if (IsClusterVersionAtleast(DocDB_V0, 102, 0))
		{
			paramOids = (Oid *) palloc(sizeof(Oid) * 3);
			paramOids[0] = BsonTypeId();
			paramOids[1] = BsonTypeId();
			paramOids[2] = BsonTypeId();
			nargs = 3;
		}
		else
		{
			paramOids = (Oid *) palloc(sizeof(Oid) * 2);
			paramOids[0] = BsonTypeId();
			paramOids[1] = BsonTypeId();
		}

		bool missingOK = false;

		Cache.ApiInternalBsonDollarMergeAddObjectIdFunctionId =
			LookupFuncName(functionNameList, nargs, paramOids, missingOK);
	}

	return Cache.ApiInternalBsonDollarMergeAddObjectIdFunctionId;
}


Oid
BsonDollarMergeGenerateObjectId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiInternalBsonDollarMergeGenerateObjectId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString(
												"bson_dollar_merge_generate_object_id"));
		Oid paramOids[1] = { BsonTypeId() };
		bool missingOK = false;

		Cache.ApiInternalBsonDollarMergeGenerateObjectId =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.ApiInternalBsonDollarMergeGenerateObjectId;
}


Oid
BsonDollarMergeFailWhenNotMatchedFunctionOid(void)
{
	bool missingOk = false;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.ApiInternalBsonDollarMergeFailWhenNotMathchedFunctionId,
		"bson_dollar_merge_fail_when_not_matched",
		BsonTypeId(),
		TEXTOID, missingOk);
}


Oid
BsonDollarMergeExtractFilterFunctionOid(void)
{
	bool missingOk = false;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.ApiInternalBsonDollarMergeExtractFilterFunctionId,
		"bson_dollar_extract_merge_filter",
		BsonTypeId(),
		TEXTOID, missingOk);
}


Oid
BsonDollarMergeJoinFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiInternalBsonDollarMergeJoinFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString(
												"bson_dollar_merge_join"));
		Oid paramOids[3] = { BsonTypeId(), BsonTypeId(), TEXTOID };
		bool missingOK = false;

		Cache.ApiInternalBsonDollarMergeJoinFunctionId =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.ApiInternalBsonDollarMergeJoinFunctionId;
}


Oid
BsonGetValueFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonGetValueFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(CoreSchemaName),
											makeString("bson_get_value"));
		Oid paramOids[2] = { BsonTypeId(), TEXTOID };
		bool missingOK = false;

		Cache.BsonGetValueFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.BsonGetValueFunctionId;
}


Oid
BsonDollarUnwindFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.ApiCatalogBsonDollarUnwindFunctionOid,
									   "bson_dollar_unwind", BsonTypeId(), TEXTOID);
}


Oid
BsonDollarUnwindWithOptionsFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(
		&Cache.ApiCatalogBsonDollarUnwindWithOptionsFunctionOid,
		"bson_dollar_unwind", BsonTypeId(), BsonTypeId());
}


Oid
BsonDollarReplaceRootFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.ApiCatalogBsonDollarReplaceRootFunctionOid,
									   "bson_dollar_replace_root", BsonTypeId(),
									   BsonTypeId());
}


Oid
BsonDollarReplaceRootWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiCatalogBsonDollarReplaceRootWithLetFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_replace_root",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(),
		DocumentDBCoreBsonTypeId());
}


Oid
BsonDollarReplaceRootWithLetAndCollationFunctionOid(void)
{
	Oid bsonTypeId = DocumentDBCoreBsonTypeId();
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonDollarReplaceRootWithLetAndCollationFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_dollar_replace_root",
		bsonTypeId, bsonTypeId, bsonTypeId, TEXTOID);
}


static Oid
GetAggregateFunctionByName(Oid *function, char *namespaceName, char *name)
{
	InitializeDocumentDBApiExtensionCache();

	if (*function == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(namespaceName),
											makeString(name));
		bool missingOK = false;
		ObjectWithArgs args = { 0 };
		args.args_unspecified = true;
		args.objname = functionNameList;

		*function = LookupFuncWithArgs(OBJECT_AGGREGATE, &args, missingOK);
	}

	return *function;
}


static Oid
GetFunctionByName(Oid *function, char *namespaceName, char *name)
{
	InitializeDocumentDBApiExtensionCache();

	if (*function == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(namespaceName),
											makeString(name));
		bool missingOK = false;
		ObjectWithArgs args = { 0 };
		args.args_unspecified = true;
		args.objname = functionNameList;

		*function = LookupFuncWithArgs(OBJECT_FUNCTION, &args, missingOK);
	}

	return *function;
}


Oid
BsonRankFunctionOid(void)
{
	return GetFunctionByName(&Cache.ApiCatalogBsonRankFunctionOid,
							 DocumentDBApiInternalSchemaName, "bson_rank");
}


Oid
BsonDenseRankFunctionOid(void)
{
	return GetFunctionByName(&Cache.ApiCatalogBsonDenseRankFunctionOid,
							 DocumentDBApiInternalSchemaName, "bson_dense_rank");
}


Oid
BsonDocumentNumberFunctionOid(void)
{
	return GetFunctionByName(&Cache.ApiCatalogBsonDocumentNumberFunctionOid,
							 DocumentDBApiInternalSchemaName, "bson_document_number");
}


Oid
BsonShiftFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiCatalogBsonShiftFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_shift",
		BsonTypeId(), INT4OID,
		BsonTypeId());
}


Oid
BsonSumAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonSumAggregateFunctionOid,
									  ApiCatalogSchemaName, "bsonsum");
}


Oid
BsonLinearFillFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogBsonLinearFillFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("bson_linear_fill"));
		Oid paramOids[2] = { BsonTypeId(), BsonTypeId() };
		bool missingOK = false;

		Cache.ApiCatalogBsonLinearFillFunctionOid =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.ApiCatalogBsonLinearFillFunctionOid;
}


Oid
BsonLocfFillFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogBsonLocfFillFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("bson_locf_fill"));
		Oid paramOids[1] = { BsonTypeId() };
		bool missingOK = false;

		Cache.ApiCatalogBsonLocfFillFunctionOid =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.ApiCatalogBsonLocfFillFunctionOid;
}


Oid
BsonConstFillFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonConstFillFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("bson_const_fill"));
		Oid paramOids[2] = { BsonTypeId(), BsonTypeId() };
		bool missingOK = false;

		Cache.BsonConstFillFunctionOid =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.BsonConstFillFunctionOid;
}


Oid
BsonIntegralAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonIntegralAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonintegral");
}


Oid
BsonDerivativeAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonDerivativeAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonderivative");
}


Oid
BsonAvgAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonAverageAggregateFunctionOid,
									  ApiCatalogSchemaName, "bsonaverage");
}


Oid
BsonCovariancePopAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonCovariancePopAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsoncovariancepop");
}


Oid
BsonCovarianceSampAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonCovarianceSampAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsoncovariancesamp");
}


static Oid
GetBsonArrayAggregateFunctionOid(Oid *function, bool allArgs)
{
	InitializeDocumentDBApiExtensionCache();

	if (*function == InvalidOid)
	{
		ObjectWithArgs *objectWithArgs = makeNode(ObjectWithArgs);
		List *functionNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString("bson_array_agg"));

		objectWithArgs->objname = functionNameList;
		objectWithArgs->objargs = list_make2(ParseTypeNameCore(FullBsonTypeName),
											 ParseTypeNameCore("text"));

		FunctionParameter *inBsonArgParam = makeNode(FunctionParameter);
		inBsonArgParam->argType = ParseTypeNameCore(FullBsonTypeName);
		inBsonArgParam->mode = FUNC_PARAM_IN;

		FunctionParameter *inFieldPathParam = makeNode(FunctionParameter);
		inFieldPathParam->argType = ParseTypeNameCore("text");
		inFieldPathParam->mode = FUNC_PARAM_IN;

		objectWithArgs->objfuncargs = list_make2(inBsonArgParam, inFieldPathParam);

		/* Add handleSingleValue argument. TODO: remove if when previous function version is deprecated. */
		if (allArgs)
		{
			objectWithArgs->objargs = lappend(objectWithArgs->objargs, ParseTypeNameCore(
												  "boolean"));

			FunctionParameter *inHandleSingleValueParam = makeNode(FunctionParameter);
			inHandleSingleValueParam->argType = ParseTypeNameCore("boolean");
			inHandleSingleValueParam->mode = FUNC_PARAM_IN;

			objectWithArgs->objfuncargs = lappend(objectWithArgs->objfuncargs,
												  inHandleSingleValueParam);
		}

		bool missingOK = false;
		*function = LookupFuncWithArgs(OBJECT_AGGREGATE, objectWithArgs, missingOK);
	}

	return *function;
}


/*
 * TODO: Remove this implementation in favor of the below.
 */
Oid
BsonArrayAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonArrayAggregateFunctionOid(
		&Cache.ApiCatalogBsonArrayAggregateFunctionOid, allArgs);
}


Oid
BsonArrayAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonArrayAggregateFunctionOid(
		&Cache.ApiCatalogBsonArrayAggregateAllArgsFunctionOid, allArgs);
}


Oid
BsonExpMovingAvgAggregateFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogBsonExpMovingAvgAggregateFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("bson_exp_moving_avg"));
		Oid paramOids[3] = { BsonTypeId(), BsonTypeId(), BOOLOID };
		bool missingOK = false;

		Cache.ApiCatalogBsonExpMovingAvgAggregateFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.ApiCatalogBsonExpMovingAvgAggregateFunctionOid;
}


Oid
BsonDistinctAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonDistinctAggregateFunctionOid,
									  ApiCatalogSchemaName, "bson_distinct_agg");
}


Oid
BsonObjectAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonObjectAggregateFunctionOid,
									  ApiCatalogSchemaName, "bson_object_agg");
}


Oid
BsonMergeObjectsOnSortedFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonMergeObjectsOnSortedFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_merge_objects_on_sorted");
}


Oid
BsonMergeObjectsFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonMergeObjectsFunctionOid,
									  DocumentDBApiInternalSchemaName,
									  "bson_merge_objects");
}


Oid
BsonMaxAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonMaxAggregateFunctionOid,
									  ApiCatalogSchemaName, "bsonmax");
}


Oid
BsonMinAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonMinAggregateFunctionOid,
									  ApiCatalogSchemaName, "bsonmin");
}


/* Returns the correct aggregate function Oid for top(N)/bottom(N)/first(N)/last(N) based on number of arguments*/
static Oid
GetBsonFirstNLastNAggregateFunctionOid(Oid *function, bool allArgs, char *aggregateName)
{
	InitializeDocumentDBApiExtensionCache();

	String *schemaName;
	if (allArgs)
	{
		schemaName = makeString(DocumentDBApiInternalSchemaName);
	}
	else
	{
		schemaName = makeString(ApiCatalogSchemaName);
	}

	if (*function == InvalidOid)
	{
		ObjectWithArgs *objectWithArgs = makeNode(ObjectWithArgs);
		List *functionNameList = list_make2(schemaName,
											makeString(aggregateName));

		objectWithArgs->objname = functionNameList;

		FunctionParameter *inBsonArgParam = makeNode(FunctionParameter);
		inBsonArgParam->argType = ParseTypeNameCore(FullBsonTypeName);
		inBsonArgParam->mode = FUNC_PARAM_IN;

		FunctionParameter *inBsonArrayArgParam = makeNode(FunctionParameter);
		StringInfo fullBsonArrayTypeName = makeStringInfo();
		appendStringInfo(fullBsonArrayTypeName, "%s[]", FullBsonTypeName);
		inBsonArrayArgParam->argType = ParseTypeNameCore(fullBsonArrayTypeName->data);
		inBsonArrayArgParam->mode = FUNC_PARAM_IN;

		if (strcmp(aggregateName, "bsonfirstn") == 0 ||
			strcmp(aggregateName, "bsonlastn") == 0)
		{
			objectWithArgs->objargs =
				list_make3(ParseTypeNameCore(FullBsonTypeName),
						   ParseTypeNameCore("bigint"),
						   ParseTypeNameCore(fullBsonArrayTypeName->data));

			FunctionParameter *inIntArgParam = makeNode(FunctionParameter);
			inIntArgParam->argType = ParseTypeNameCore("bigint");
			inIntArgParam->mode = FUNC_PARAM_IN;

			objectWithArgs->objfuncargs = list_make3(inBsonArgParam, inIntArgParam,
													 inBsonArrayArgParam);
		}
		else
		{
			objectWithArgs->objargs =
				list_make2(ParseTypeNameCore(FullBsonTypeName),
						   ParseTypeNameCore(fullBsonArrayTypeName->data));
			objectWithArgs->objfuncargs = list_make2(inBsonArgParam, inBsonArrayArgParam);
		}

		if (allArgs)
		{
			objectWithArgs->objargs = lappend(objectWithArgs->objargs, ParseTypeNameCore(
												  FullBsonTypeName));

			FunctionParameter *inBsonArgExpressionParam = makeNode(FunctionParameter);
			inBsonArgExpressionParam->argType = ParseTypeNameCore(FullBsonTypeName);
			inBsonArgExpressionParam->mode = FUNC_PARAM_IN;

			objectWithArgs->objfuncargs = lappend(objectWithArgs->objfuncargs,
												  inBsonArgExpressionParam);
		}

		bool missingOK = false;
		*function = LookupFuncWithArgs(OBJECT_AGGREGATE, objectWithArgs, missingOK);
	}

	return *function;
}


/* Returns the correct aggregate function Oid for top(N)/bottom(N)/first(N)/last(N) based on number of arguments*/
static Oid
GetBsonFirstNLastNOnSortedAggregateFunctionOid(Oid *function, bool allArgs,
											   char *aggregateName)
{
	InitializeDocumentDBApiExtensionCache();

	String *schemaName;
	if (allArgs)
	{
		schemaName = makeString(DocumentDBApiInternalSchemaName);
	}
	else
	{
		schemaName = makeString(ApiCatalogSchemaName);
	}

	if (*function == InvalidOid)
	{
		ObjectWithArgs *objectWithArgs = makeNode(ObjectWithArgs);
		List *functionNameList = list_make2(schemaName,
											makeString(aggregateName));

		objectWithArgs->objname = functionNameList;

		FunctionParameter *inBsonArgParam = makeNode(FunctionParameter);
		inBsonArgParam->argType = ParseTypeNameCore(FullBsonTypeName);
		inBsonArgParam->mode = FUNC_PARAM_IN;

		if (strcmp(aggregateName, "bsonfirstnonsorted") == 0 ||
			strcmp(aggregateName, "bsonlastnonsorted") == 0)
		{
			objectWithArgs->objargs =
				list_make2(ParseTypeNameCore(FullBsonTypeName),
						   ParseTypeNameCore("bigint"));

			FunctionParameter *inIntArgParam = makeNode(FunctionParameter);
			inIntArgParam->argType = ParseTypeNameCore("bigint");
			inIntArgParam->mode = FUNC_PARAM_IN;

			objectWithArgs->objfuncargs = list_make2(inBsonArgParam, inIntArgParam);
		}
		else
		{
			objectWithArgs->objargs =
				list_make1(ParseTypeNameCore(FullBsonTypeName));
			objectWithArgs->objfuncargs = list_make1(inBsonArgParam);
		}

		if (allArgs)
		{
			objectWithArgs->objargs = lappend(objectWithArgs->objargs, ParseTypeNameCore(
												  FullBsonTypeName));

			FunctionParameter *inBsonArgExpressionParam = makeNode(FunctionParameter);
			inBsonArgExpressionParam->argType = ParseTypeNameCore(FullBsonTypeName);
			inBsonArgExpressionParam->mode = FUNC_PARAM_IN;

			objectWithArgs->objfuncargs = lappend(objectWithArgs->objfuncargs,
												  inBsonArgExpressionParam);
		}

		bool missingOK = false;
		*function = LookupFuncWithArgs(OBJECT_AGGREGATE, objectWithArgs, missingOK);
	}

	return *function;
}


Oid
BsonFirstOnSortedAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.ApiCatalogBsonFirstOnSortedAggregateFunctionOid, allArgs,
		"bsonfirstonsorted");
}


Oid
BsonFirstOnSortedAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.BsonFirstOnSortedAggregateAllArgsFunctionOid, allArgs,
		"bsonfirstonsorted");
}


Oid
BsonFirstAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.ApiCatalogBsonFirstAggregateFunctionOid, allArgs, "bsonfirst");
}


Oid
BsonFirstAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.BsonFirstAggregateAllArgsFunctionOid, allArgs, "bsonfirst");
}


Oid
BsonLastAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.ApiCatalogBsonLastAggregateFunctionOid, allArgs, "bsonlast");
}


Oid
BsonLastAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.BsonLastAggregateAllArgsFunctionOid, allArgs, "bsonlast");
}


Oid
BsonLastOnSortedAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.ApiCatalogBsonLastOnSortedAggregateFunctionOid, allArgs,
		"bsonlastonsorted");
}


Oid
BsonLastOnSortedAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.BsonLastOnSortedAggregateAllArgsFunctionOid, allArgs,
		"bsonlastonsorted");
}


Oid
BsonFirstNAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.ApiCatalogBsonFirstNAggregateFunctionOid, allArgs, "bsonfirstn");
}


Oid
BsonFirstNAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.BsonFirstNAggregateAllArgsFunctionOid, allArgs, "bsonfirstn");
}


Oid
BsonFirstNOnSortedAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.ApiCatalogBsonFirstNOnSortedAggregateFunctionOid, allArgs,
		"bsonfirstnonsorted");
}


Oid
BsonFirstNOnSortedAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.BsonFirstNOnSortedAggregateAllArgsFunctionOid, allArgs,
		"bsonfirstnonsorted");
}


Oid
BsonLastNAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.ApiCatalogBsonLastNAggregateFunctionOid, allArgs, "bsonlastn");
}


Oid
BsonLastNAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNAggregateFunctionOid(
		&Cache.BsonLastNAggregateAllArgsFunctionOid, allArgs, "bsonlastn");
}


Oid
BsonLastNOnSortedAggregateFunctionOid(void)
{
	bool allArgs = false;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.ApiCatalogBsonLastNOnSortedAggregateFunctionOid, allArgs,
		"bsonlastnonsorted");
}


Oid
BsonLastNOnSortedAggregateAllArgsFunctionOid(void)
{
	bool allArgs = true;
	return GetBsonFirstNLastNOnSortedAggregateFunctionOid(
		&Cache.BsonLastNOnSortedAggregateAllArgsFunctionOid, allArgs,
		"bsonlastnonsorted");
}


Oid
BsonMaxNAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonMaxNAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonmaxn");
}


Oid
BsonMinNAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonMinNAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonminn");
}


Oid
BsonStdDevPopAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonStdDevPopAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonstddevpop");
}


Oid
BsonStdDevSampAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonStdDevSampAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonstddevsamp");
}


Oid
BsonMedianAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonMedianAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonmedian");
}


Oid
BsonPercentileAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(
		&Cache.ApiCatalogBsonPercentileAggregateFunctionOid,
		DocumentDBApiInternalSchemaName, "bsonpercentile");
}


Oid
BsonAddToSetAggregateFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.ApiCatalogBsonAddToSetAggregateFunctionOid,
									  DocumentDBApiInternalSchemaName, "bson_add_to_set");
}


Oid
PostgresAnyValueFunctionOid(void)
{
	return GetAggregateFunctionByName(&Cache.PostgresAnyValueFunctionOid, "pg_catalog",
									  "any_value");
}


Oid
PgRandomFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PgRandomFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("pg_catalog"),
											makeString("random"));
		Oid *paramOids = NULL;
		bool missingOK = false;

		Cache.PgRandomFunctionOid =
			LookupFuncName(functionNameList, 0, paramOids, missingOK);
	}

	return Cache.PgRandomFunctionOid;
}


Oid
BsonLookupExtractFilterExpressionFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(
		&Cache.ApiCatalogBsonLookupExtractFilterExpressionOid,
		"bson_dollar_lookup_extract_filter_expression",
		BsonTypeId(), BsonTypeId());
}


Oid
BsonLookupExtractFilterArrayFunctionOid(void)
{
	bool missingOk = false;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.ApiCatalogBsonLookupExtractFilterArrayOid,
		"bson_dollar_lookup_extract_filter_array",
		BsonTypeId(), BsonTypeId(), missingOk);
}


Oid
DocumentDBApiInternalBsonLookupExtractFilterExpressionFunctionOid(void)
{
	bool missingOk = false;
	return GetDocumentDBInternalBinaryOperatorFunctionId(
		&Cache.DocumentDBInternalBsonLookupExtractFilterExpressionOid,
		"bson_dollar_lookup_extract_filter_expression",
		BsonTypeId(), BsonTypeId(), missingOk);
}


Oid
BsonDollarLookupJoinFilterFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonDollarLookupJoinFilterFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString("bson_dollar_lookup_join_filter"));
		Oid paramOids[3] = { BsonTypeId(), BsonTypeId(), TEXTOID };
		bool missingOK = false;

		Cache.BsonDollarLookupJoinFilterFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.BsonDollarLookupJoinFilterFunctionOid;
}


Oid
BsonLookupUnwindFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonLookupUnwindFunctionOid,
									   "bson_lookup_unwind",
									   BsonTypeId(), TEXTOID);
}


Oid
BsonDistinctUnwindFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonDistinctUnwindFunctionOid,
									   "bson_distinct_unwind",
									   BsonTypeId(), TEXTOID);
}


Oid
BsonRepathAndBuildFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogBsonRepathAndBuildFunctionOid == InvalidOid)
	{
		/* Given it's a variadic function, we just look it up by name */
		List *functionNameList = list_make2(makeString(CoreSchemaName),
											makeString("bson_repath_and_build"));
		bool missingOK = false;
		ObjectWithArgs args = { 0 };
		args.args_unspecified = true;
		args.objname = functionNameList;

		Cache.ApiCatalogBsonRepathAndBuildFunctionOid =
			LookupFuncWithArgs(OBJECT_FUNCTION, &args, missingOK);
	}

	return Cache.ApiCatalogBsonRepathAndBuildFunctionOid;
}


Oid
RowGetBsonFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiCatalogRowGetBsonFunctionOid == InvalidOid)
	{
		/* Given it's a variadic function, we just look it up by name */
		List *functionNameList = list_make2(makeString(CoreSchemaName),
											makeString("row_get_bson"));
		bool missingOK = false;
		ObjectWithArgs args = { 0 };
		args.args_unspecified = true;
		args.objname = functionNameList;

		Cache.ApiCatalogRowGetBsonFunctionOid =
			LookupFuncWithArgs(OBJECT_FUNCTION, &args, missingOK);
	}

	return Cache.ApiCatalogRowGetBsonFunctionOid;
}


Oid
BsonExpressionGetFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(&Cache.ApiCatalogBsonExpressionGetFunctionOid,
										  ApiCatalogSchemaName, "bson_expression_get",
										  BsonTypeId(), BsonTypeId(), BOOLOID);
}


Oid
BsonExpressionGetWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonExpressionGetWithLetFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_expression_get",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(), BOOLOID,
		DocumentDBCoreBsonTypeId());
}


Oid
BsonExpressionPartitionGetFunctionOid(void)
{
	return GetOperatorFunctionIdThreeArgs(
		&Cache.ApiCatalogBsonExpressionPartitionGetFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_expression_partition_get",
		BsonTypeId(), BsonTypeId(), BOOLOID);
}


Oid
BsonExpressionPartitionByFieldsGetFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonExpressionPartitionByFieldsGetFunctionOid,
		DocumentDBApiInternalSchemaName,
		"bson_expression_partition_by_fields_get", nargs,
		argTypes,
		missingOk);
}


Oid
BsonExpressionPartitionGetWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonExpressionPartitionGetWithLetFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_expression_partition_get",
		DocumentDBCoreBsonTypeId(), DocumentDBCoreBsonTypeId(), BOOLOID,
		DocumentDBCoreBsonTypeId());
}


Oid
BsonExpressionMapFunctionOid(void)
{
	return GetOperatorFunctionIdFourArgs(
		&Cache.ApiCatalogBsonExpressionMapFunctionOid,
		ApiCatalogSchemaName, "bson_expression_map",
		BsonTypeId(), TEXTOID, BsonTypeId(), BOOLOID);
}


Oid
BsonExpressionMapWithLetFunctionOid(void)
{
	return GetOperatorFunctionIdFiveArgs(
		&Cache.ApiCatalogBsonExpressionMapWithLetFunctionOid,
		DocumentDBApiInternalSchemaName, "bson_expression_map",
		DocumentDBCoreBsonTypeId(), TEXTOID, DocumentDBCoreBsonTypeId(), BOOLOID,
		DocumentDBCoreBsonTypeId());
}


/*
 * GeometryTypeId returns the OID of the PostgisSchemaName.geometry type.
 */
Oid
GeometryTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.GeometryTypeId == InvalidOid)
	{
		List *geometryTypeNameList = list_make2(makeString(PostgisSchemaName),
												makeString(
													"geometry"));
		TypeName *geometryTypeName = makeTypeNameFromNameList(geometryTypeNameList);
		Cache.GeometryTypeId = typenameTypeId(NULL, geometryTypeName);
	}

	return Cache.GeometryTypeId;
}


/*
 * Box2df returns postgis box2df type id
 */
Oid
Box2dfTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Box2dfTypeId == InvalidOid)
	{
		List *typeNameList = list_make2(makeString(PostgisSchemaName),
										makeString("box2df"));
		TypeName *typeName = makeTypeNameFromNameList(typeNameList);
		Cache.Box2dfTypeId = typenameTypeId(NULL, typeName);
	}

	return Cache.Box2dfTypeId;
}


/*
 * GeographyTypeId returns the OID of the PostgisSchemaName.geography type.
 */
Oid
GeographyTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.GeographyTypeId == InvalidOid)
	{
		List *geographyTypeNameList = list_make2(makeString(PostgisSchemaName),
												 makeString(
													 "geography"));
		TypeName *geographyTypeName = makeTypeNameFromNameList(geographyTypeNameList);
		Cache.GeographyTypeId = typenameTypeId(NULL, geographyTypeName);
	}

	return Cache.GeographyTypeId;
}


/*
 * GIDXTypeId returns postgis gidx type id
 */
Oid
GIDXTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.GIDXTypeId == InvalidOid)
	{
		List *typeNameList = list_make2(makeString(PostgisSchemaName),
										makeString("gidx"));
		TypeName *typeName = makeTypeNameFromNameList(typeNameList);
		Cache.GIDXTypeId = typenameTypeId(NULL, typeName);
	}

	return Cache.GIDXTypeId;
}


/*
 * GeometryArrayTypeId returns the array type id of PostgisSchemaName.geometry type
 */
Oid
GeometryArrayTypeId(void)
{
	return GetArrayTypeOid(&Cache.GeometryArrayTypeId, GeometryTypeId());
}


/*
 * VectorTypeId returns the OID of the vector type.
 */
Oid
VectorTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorTypeId == InvalidOid)
	{
		List *vectorTypeNameList = list_make2(makeString("public"), makeString("vector"));
		TypeName *vectorTypeName = makeTypeNameFromNameList(vectorTypeNameList);
		Cache.VectorTypeId = typenameTypeId(NULL, vectorTypeName);
	}

	return Cache.VectorTypeId;
}


/*
 * IndexSpecTypeId returns the OID of the ApiCatalogSchemaName.index_spec_type.
 */
Oid
IndexSpecTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.IndexSpecTypeId == InvalidOid)
	{
		List *typeNameList = list_make2(makeString(ApiCatalogSchemaName),
										makeString("index_spec_type"));
		TypeName *typeName = makeTypeNameFromNameList(typeNameList);
		Cache.IndexSpecTypeId = typenameTypeId(NULL, typeName);
	}

	return Cache.IndexSpecTypeId;
}


Oid
MongoCatalogCollectionsTypeOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.MongoCatalogCollectionsTypeOid == InvalidOid)
	{
		List *typeNameList = list_make2(makeString(ApiCatalogSchemaName),
										makeString("collections"));
		TypeName *typeName = makeTypeNameFromNameList(typeNameList);
		Cache.MongoCatalogCollectionsTypeOid = typenameTypeId(NULL, typeName);
	}

	return Cache.MongoCatalogCollectionsTypeOid;
}


/*
 * BsonOrderByFunctionId returns the OID of the bson_orderby(<bson>, <bson>) function.
 */
Oid
BsonOrderByFunctionOid(void)
{
	return GetBinaryOperatorFunctionId(&Cache.BsonOrderByFunctionId,
									   "bson_orderby", BsonTypeId(), BsonTypeId());
}


/*
 * BsonOrderByPartitionFunctionOid returns the OID of the bson_orderby_partition(<bson>, <bson>, bool) function.
 */
Oid
BsonOrderByPartitionFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonOrderByPartitionFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString(
												"bson_orderby_partition"));
		Oid paramOids[3] = { BsonTypeId(), BsonTypeId(), BOOLOID };
		bool missingOK = false;

		Cache.BsonOrderByPartitionFunctionOid =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.BsonOrderByPartitionFunctionOid;
}


Oid
ApiCatalogBsonExtractVectorFunctionId(void)
{
	return GetBinaryOperatorFunctionIdWithSchema(
		&Cache.ApiCatalogBsonExtractVectorFunctionId,
		"bson_extract_vector", BsonTypeId(),
		TEXTOID, ApiCatalogToApiInternalSchemaName);
}


/*
 * Returns the OID of the ApiSchemaName.bson_search_param function.
 */
Oid
ApiBsonSearchParamFunctionId(void)
{
	return GetBinaryOperatorFunctionIdWithSchema(&Cache.ApiBsonSearchParamFunctionId,
												 "bson_search_param", BsonTypeId(),
												 BsonTypeId(),
												 ApiCatalogToApiInternalSchemaName);
}


/*
 * Returns the OID of the bson_document_add_score_field function.
 */
Oid
ApiBsonDocumentAddScoreFieldFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiBsonDocumentAddScoreFieldFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogToApiInternalSchemaName),
											makeString(
												"bson_document_add_score_field"));
		Oid paramOids[2] = { BsonTypeId(), FLOAT8OID };
		bool missingOK = false;

		Cache.ApiBsonDocumentAddScoreFieldFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.ApiBsonDocumentAddScoreFieldFunctionId;
}


/*
 * BsonDollarGeowithinFunctionOid returns the OID of ApiCatalogSchemaName.bson_dollar_geowithin
 */
Oid
BsonDollarGeowithinFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = true;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonDollarGeowithinFunctionOid,
		ApiCatalogSchemaName, "bson_dollar_geowithin", nargs,
		argTypes, missingOk);
}


Oid
BsonDollarGeoIntersectsFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = true;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonDollarGeoIntersectsFunctionOid,
		ApiCatalogSchemaName, "bson_dollar_geointersects", nargs,
		argTypes, missingOk);
}


/*
 * BsonValidateGeometryFunctionId returns the OID of the ApiCatalogSchemaName.bson_validate_geometry
 */
Oid
BsonValidateGeometryFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), TEXTOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonValidateGeometryFunctionId,
		ApiCatalogSchemaName, "bson_validate_geometry", nargs,
		argTypes, missingOk);
}


/*
 * BsonValidateGeographyFunctionId returns the OID of the ApiCatalogSchemaName.bson_validate_geography
 */
Oid
BsonValidateGeographyFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), TEXTOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonValidateGeographyFunctionId,
		ApiCatalogSchemaName, "bson_validate_geography", nargs,
		argTypes, missingOk);
}


Oid
DocumentDBCoreBsonToBsonFunctionOId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { DocumentDBCoreBsonTypeId() };

	char *functionName = psprintf("%s_core_bson_to_bson", ExtensionObjectPrefixV2);

	return GetSchemaFunctionIdWithNargs(&Cache.DocumentDBCoreBsonToBsonFunctionOId,
										ApiInternalSchemaNameV2,
										functionName, nargs,
										argTypes, false);
}


/*
 * PostgisGeometryGistCompress2dFunctionId returns OID of PostgisSchemaName.geometry_gist_compress_2d
 */
Oid
PostgisGeometryGistCompress2dFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { INTERNALOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryGistCompress2dFunctionId,
		PostgisSchemaName, "geometry_gist_compress_2d", nargs,
		argTypes, missingOk);
}


/*
 * PostgisForcePolygonCWFunctionId returns OID of PostgisSchemaName.ST_ForcePolygonCW
 */
Oid
PostgisForcePolygonCWFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisForcePolygonCWFunctionId,
		PostgisSchemaName, "st_forcepolygoncw", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryAsBinaryFunctionId returns OID of PostgisSchemaName.ST_AsBinary
 */
Oid
PostgisGeometryAsBinaryFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryAsBinaryFunctionId,
		PostgisSchemaName, "st_asbinary", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyGistCompressFunctionId returns OID of postgis_public.geography_gist_compress
 */
Oid
PostgisGeographyGistCompressFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { INTERNALOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyGistCompressFunctionId,
		PostgisSchemaName, "geography_gist_compress", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryGistConsistent2dFunctionId returns OID of PostgisSchemaName.geometry_gist_consistent_2d
 */
Oid
PostgisGeometryGistConsistent2dFunctionId(void)
{
	int nargs = 3;
	Oid argTypes[3] = { INTERNALOID, GeometryTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryGistConsistent2dFunctionId,
		PostgisSchemaName, "geometry_gist_consistent_2d", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyGistConsistentFunctionId returns OID of PostgisSchemaName.geography_gist_consistent
 */
Oid
PostgisGeographyGistConsistentFunctionId(void)
{
	int nargs = 3;
	Oid argTypes[3] = { INTERNALOID, GeographyTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyGistConsistentFunctionId,
		PostgisSchemaName, "geography_gist_consistent", nargs,
		argTypes, missingOk);
}


/*
 * PostgisMakeEnvelopeFunctionId returns the OID of the PostgisSchemaName.st_makeenvelope function.
 */
Oid
PostgisMakeEnvelopeFunctionId(void)
{
	int nargs = 5;
	Oid argTypes[5] = { FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID, INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisMakeEnvelopeFunctionId,
		PostgisSchemaName, "st_makeenvelope", nargs,
		argTypes, missingOk);
}


/*
 * PostgisMakePointFunctionId returns the OID of the PostgisSchemaName.st_makepoint function.
 */
Oid
PostgisMakePointFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { FLOAT8OID, FLOAT8OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisMakePointFunctionId,
		PostgisSchemaName, "st_makepoint", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryBufferFunctionId returns the OID of the PostgisSchemaName.st_buffer function for geometry.
 */
Oid
PostgisGeometryBufferFunctionId(void)
{
	int nargs = 3;
	Oid argTypes[3] = { GeometryTypeId(), FLOAT8OID, TEXTOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryBufferFunctionId,
		PostgisSchemaName, "st_buffer", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyBufferFunctionId returns the OID of the PostgisSchemaName.st_buffer function for geography.
 */
Oid
PostgisGeographyBufferFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { GeographyTypeId(), FLOAT8OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyBufferFunctionId,
		PostgisSchemaName, "st_buffer", nargs,
		argTypes, missingOk);
}


/*
 * PostgisMakeLineFunctionId returns the OID of the PostgisSchemaName.st_makepolygon function.
 */
Oid
PostgisMakePolygonFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisMakePolygonFunctionId,
		PostgisSchemaName, "st_makepolygon", nargs,
		argTypes, missingOk);
}


/*
 * PostgisMakeLineFunctionId returns the OID of the PostgisSchemaName.st_makeline function.
 */
Oid
PostgisMakeLineFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryArrayTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisMakeLineFunctionId,
		PostgisSchemaName, "st_makeline", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyFromWKBFunctionId returns the OID of the PostgisSchemaName.st_geogfromwkb function.
 * which converts the WKB to a geography
 */
Oid
PostgisGeographyFromWKBFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { BYTEAOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyFromWKBFunctionId,
		PostgisSchemaName, "st_geogfromwkb", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyCoversFunctionId returns the OID of PostgisSchemaName.st_covers function.
 * Note this variant is only used for geographies
 */
Oid
PostgisGeographyCoversFunctionId(void)
{
	int nargs = 2;
	Oid geographyTypeId = GeographyTypeId();
	Oid argTypes[2] = { geographyTypeId, geographyTypeId };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyCoversFunctionId,
		PostgisSchemaName, "st_covers", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyDWithinFunctionId returns the OID of PostgisSchemaName.st_dwithin function.
 * Note this variant is only used for geographies
 */
Oid
PostgisGeographyDWithinFunctionId(void)
{
	int nargs = 4;
	Oid geographyTypeId = GeographyTypeId();
	Oid argTypes[4] = { geographyTypeId, geographyTypeId, FLOAT8OID, BOOLOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyDWithinFunctionId,
		PostgisSchemaName, "st_dwithin", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryDistanceCentroidFunctionId returns the OID of PostgisSchemaName.geometry_distance_centroid function.
 */
Oid
PostgisGeometryDistanceCentroidFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { GeometryTypeId(), GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryDistanceCentroidFunctionId,
		PostgisSchemaName, "geometry_distance_centroid", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyDistanceKNNFunctionId returns the OID of PostgisSchemaName.geography_distance_knn function.
 */
Oid
PostgisGeographyDistanceKNNFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { GeographyTypeId(), GeographyTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyDistanceKNNFunctionId,
		PostgisSchemaName, "geography_distance_knn", nargs,
		argTypes, missingOk);
}


/*
 * BsonGistGeographyDistanceFunctionOid returns the OID of bson_gist_geography_distance
 */
Oid
BsonGistGeographyDistanceFunctionOid(void)
{
	int nargs = 3;
	Oid argTypes[3] = { INTERNALOID, BsonTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonGistGeographyDistanceFunctionOid,
		ApiCatalogSchemaName, "bson_gist_geography_distance", nargs,
		argTypes, missingOk);
}


/*
 * BsonGistGeographyDistanceFunctionOid returns the OID of bson_gist_geography_distance
 */
Oid
BsonGistGeographyConsistentFunctionOid(void)
{
	int nargs = 3;
	Oid argTypes[3] = { INTERNALOID, BsonTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonGistGeographyConsistentFunctionOid,
		ApiCatalogSchemaName, "bson_gist_geography_consistent", nargs,
		argTypes, missingOk);
}


/*
 * Returns oid of ApiCatalogSchemaName.bson_dollar_project_geonear function
 */
Oid
BsonDollarProjectGeonearFunctionOid(void)
{
	int nargs = 2;
	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.BsonDollarProjectGeonearFunctionOid,
		ApiCatalogSchemaName, "bson_dollar_project_geonear", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryGistDistanceFunctionId returns OID of PostgisSchemaName.geometry_gist_distance_2d
 */
Oid
PostgisGeometryGistDistanceFunctionId(void)
{
	int nargs = 3;
	Oid argTypes[3] = { INTERNALOID, GeometryTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryGistDistanceFunctionId,
		PostgisSchemaName, "geometry_gist_distance_2d", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyGistDistanceFunctionId returns OID of PostgisSchemaName.geography_gist_distance
 */
Oid
PostgisGeographyGistDistanceFunctionId(void)
{
	int nargs = 3;
	Oid argTypes[3] = { INTERNALOID, GeographyTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyGistDistanceFunctionId,
		PostgisSchemaName, "geography_gist_distance", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryDWithinFunctionId returns the OID of PostgisSchemaName.st_dwithin function.
 * Note this variant is only used for geometries
 */
Oid
PostgisGeometryDWithinFunctionId(void)
{
	int nargs = 3;
	Oid geometryTypeId = GeometryTypeId();
	Oid argTypes[3] = { geometryTypeId, geometryTypeId, FLOAT8OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryDWithinFunctionId,
		PostgisSchemaName, "st_dwithin", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGIDXGeographyOverlapsFunctionId returns the OID of PostgisSchemaName.overlaps_geog function.
 * which check gidx overlap between (gidx, geography)
 */
Oid
PostgisGIDXGeographyOverlapsFunctionId(void)
{
	int nargs = 2;
	Oid geographyTypeId = GeographyTypeId();
	Oid argTypes[2] = { GIDXTypeId(), geographyTypeId };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGIDXGeographyOverlapsFunctionId,
		PostgisSchemaName, "overlaps_geog", nargs,
		argTypes, missingOk);
}


/*
 * PostgisBox2dfGeometryOverlapsFunctionId returns the OID of PostgisSchemaName.overlaps_2d function.
 * which check box2df overlap between (box2df, geometry)
 */
Oid
PostgisBox2dfGeometryOverlapsFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { Box2dfTypeId(), GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisBox2dfGeometryOverlapsFunctionId,
		PostgisSchemaName, "overlaps_2d", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryCoversFunctionId returns the OID of PostgisSchemaName.st_covers function.
 * Note this variant is only used for geometries
 */
Oid
PostgisGeometryCoversFunctionId(void)
{
	int nargs = 2;
	Oid geometryOid = GeometryTypeId();
	Oid argTypes[2] = { geometryOid, geometryOid };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryCoversFunctionId,
		PostgisSchemaName, "st_covers", nargs,
		argTypes, missingOk);
}


/*
 * PostgisIntersectsFunctionId returns the OID of PostgisSchemaName.st_intersects function.
 * Note this variant is only used for geographies
 */
Oid
PostgisGeographyIntersectsFunctionId(void)
{
	int nargs = 2;
	Oid geographyOid = GeographyTypeId();
	Oid argTypes[2] = { geographyOid, geographyOid };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyIntersectsFunctionId,
		PostgisSchemaName, "st_intersects", nargs,
		argTypes, missingOk);
}


/*
 * PostgisIntersectsFunctionId returns the OID of PostgisSchemaName.st_intersects function.
 * Note this variant is only used for geometries
 */
Oid
PostgisGeometryIntersectsFunctionId(void)
{
	int nargs = 2;
	Oid geometryOid = GeometryTypeId();
	Oid argTypes[2] = { geometryOid, geometryOid };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryIntersectsFunctionId,
		PostgisSchemaName, "st_intersects", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryAreaFunctionId returns the OID of the PostgisSchemaName.st_area function.
 */
Oid
PostgisGeometryAreaFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryAreaFunctionId,
		PostgisSchemaName, "st_area", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryFromEWKBFunctionId returns the OID of the PostgisSchemaName.st_geomfromewkb function.
 * which converts the EWKB to a geometry
 */
Oid
PostgisGeometryFromEWKBFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { BYTEAOID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryFromEWKBFunctionId,
		PostgisSchemaName, "st_geomfromewkb", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryAsGeography returns the OID of the geometry::geography Cast function
 * PostgisSchemaName.geography(geometry).
 */
Oid
PostgisGeometryAsGeography(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryAsGeography,
		PostgisSchemaName, "geography", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryIsValidDetailFunctionId returns the OID of the PostgisSchemaName.st_isvaliddetail function.
 */
Oid
PostgisGeometryIsValidDetailFunctionId(void)
{
	int nargs = 2;
	Oid argTypes[2] = { GeometryTypeId(), INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryIsValidDetailFunctionId,
		PostgisSchemaName, "st_isvaliddetail", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryMakeValidFunctionId returns the OID of the PostgisSchemaName.st_makevalid function.
 */
Oid
PostgisGeometryMakeValidFunctionId(void)
{
	int nargs = 1;
	Oid argTypes[1] = { GeometryTypeId() };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryMakeValidFunctionId,
		PostgisSchemaName, "st_makevalid", nargs,
		argTypes, missingOk);
}


/*
 * PostgisSetSRIDFunctionId returns the OID of the PostgisSchemaName.st_setsrid function.
 */
Oid
PostgisSetSRIDFunctionId(void)
{
	int nargs = 2;
	Oid geometryOid = GeometryTypeId();
	Oid argTypes[2] = { geometryOid, INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisSetSRIDFunctionId,
		PostgisSchemaName, "st_setsrid", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeometryExpandFunctionId returns the OID of the PostgisSchemaName.st_expand function.
 */
Oid
PostgisGeometryExpandFunctionId(void)
{
	int nargs = 2;
	Oid geometryOid = GeometryTypeId();
	Oid argTypes[2] = { geometryOid, FLOAT8OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeometryExpandFunctionId,
		PostgisSchemaName, "st_expand", nargs,
		argTypes, missingOk);
}


/*
 * PostgisGeographyExpandFunctionId returns the OID of the PostgisSchemaName._st_expand function.
 * Only expands the bounding box, the actual geography will remain unchanged.
 */
Oid
PostgisGeographyExpandFunctionId(void)
{
	int nargs = 2;
	Oid geographyOid = GeographyTypeId();
	Oid argTypes[2] = { geographyOid, FLOAT8OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(
		&Cache.PostgisGeographyExpandFunctionId,
		PostgisSchemaName, "_st_expand", nargs,
		argTypes, missingOk);
}


/*
 * VectorOrderByQueryOperatorId returns the OID of the <bson> |-<>| <bson> operator.
 */
Oid
VectorOrderByQueryOperatorId(void)
{
	return GetBinaryOperatorId(&Cache.VectorOrderByQueryOperatorId,
							   BsonTypeId(), "|=<>|", BsonTypeId());
}


/*
 * Float8PlusOperatorId returns the OID of the <float8> + <float8> operator.
 */
Oid
Float8PlusOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Float8PlusOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"), makeString("+"));

		Cache.Float8PlusOperatorId =
			OpernameGetOprid(operatorNameList, FLOAT8OID, FLOAT8OID);
	}

	return Cache.Float8PlusOperatorId;
}


/*
 * Float8MinusOperatorId returns the OID of the <float8> - <float8> operator.
 */
Oid
Float8MinusOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Float8MinusOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"), makeString("-"));

		Cache.Float8MinusOperatorId =
			OpernameGetOprid(operatorNameList, FLOAT8OID, FLOAT8OID);
	}

	return Cache.Float8MinusOperatorId;
}


/*
 * Float8MultiplyOperatorId returns the OID of the <float8> * <float8> operator.
 */
Oid
Float8MultiplyOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.Float8MultiplyOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("pg_catalog"), makeString("*"));

		Cache.Float8MultiplyOperatorId =
			OpernameGetOprid(operatorNameList, FLOAT8OID, FLOAT8OID);
	}

	return Cache.Float8MultiplyOperatorId;
}


/*
 * VectorCosineSimilaritySearchOperatorId returns the OID of the <vector> <=> <vector> operator.
 */
Oid
VectorCosineSimilaritySearchOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorCosineSimilaritySearchOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("public"), makeString("<=>"));

		Cache.VectorCosineSimilaritySearchOperatorId =
			OpernameGetOprid(operatorNameList, VectorTypeId(), VectorTypeId());
	}

	return Cache.VectorCosineSimilaritySearchOperatorId;
}


/*
 * VectorL2SimilaritySearchOperatorId returns the OID of the <vector> <-> <vector> operator.
 */
Oid
VectorL2SimilaritySearchOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorL2SimilaritySearchOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("public"), makeString("<->"));

		Cache.VectorL2SimilaritySearchOperatorId =
			OpernameGetOprid(operatorNameList, VectorTypeId(), VectorTypeId());
	}

	return Cache.VectorL2SimilaritySearchOperatorId;
}


/*
 * VectorIPSimilaritySearchOperatorId returns the OID of the <vector> <#> <vector> operator.
 */
Oid
VectorIPSimilaritySearchOperatorId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorIPSimilaritySearchOperatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString("public"), makeString("<#>"));

		Cache.VectorIPSimilaritySearchOperatorId =
			OpernameGetOprid(operatorNameList, VectorTypeId(), VectorTypeId());
	}

	return Cache.VectorIPSimilaritySearchOperatorId;
}


/* OID of the cosine_distance(vector, vector) function */
Oid
VectorCosineSimilaritySearchFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorCosineSimilaritySearchFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("public"), makeString(
												"cosine_distance"));
		Oid paramOids[2] = { VectorTypeId(), VectorTypeId() };
		bool missingOK = false;

		Cache.VectorCosineSimilaritySearchFunctionId = LookupFuncName(functionNameList, 2,
																	  paramOids,
																	  missingOK);
	}

	return Cache.VectorCosineSimilaritySearchFunctionId;
}


/* OID of the l2_distance(vector, vector) function */
Oid
VectorL2SimilaritySearchFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorL2SimilaritySearchFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("public"), makeString(
												"l2_distance"));
		Oid paramOids[2] = { VectorTypeId(), VectorTypeId() };
		bool missingOK = false;

		Cache.VectorL2SimilaritySearchFunctionId = LookupFuncName(functionNameList, 2,
																  paramOids, missingOK);
	}

	return Cache.VectorL2SimilaritySearchFunctionId;
}


/* OID of the vector_negative_inner_product(vector, vector) function */
Oid
VectorIPSimilaritySearchFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorIPSimilaritySearchFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("public"), makeString(
												"vector_negative_inner_product"));
		Oid paramOids[2] = { VectorTypeId(), VectorTypeId() };
		bool missingOK = false;

		Cache.VectorIPSimilaritySearchFunctionId = LookupFuncName(functionNameList, 2,
																  paramOids, missingOK);
	}

	return Cache.VectorIPSimilaritySearchFunctionId;
}


/*
 * VectorIVFFlatCosineSimilarityOperatorFamilyId returns
 * the OID of the vector_cosine_ops operator class for access method ivfflat.
 */
Oid
VectorIVFFlatCosineSimilarityOperatorFamilyId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorIVFFlatCosineSimilarityOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.VectorIVFFlatCosineSimilarityOperatorFamilyId = get_opfamily_oid(
			PgVectorIvfFlatIndexAmId(), list_make2(makeString("public"), makeString(
													   "vector_cosine_ops")),
			missingOk);
	}

	return Cache.VectorIVFFlatCosineSimilarityOperatorFamilyId;
}


/*
 * VectorHNSWCosineSimilarityOperatorFamilyId returns
 * the OID of the vector_cosine_ops operator class for access method hnsw.
 */
Oid
VectorHNSWCosineSimilarityOperatorFamilyId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorHNSWCosineSimilarityOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.VectorHNSWCosineSimilarityOperatorFamilyId = get_opfamily_oid(
			PgVectorHNSWIndexAmId(), list_make2(makeString("public"), makeString(
													"vector_cosine_ops")),
			missingOk);
	}

	return Cache.VectorHNSWCosineSimilarityOperatorFamilyId;
}


/*
 * VectorIVFFlatL2SimilarityOperatorFamilyId returns
 * the OID of the vector_l2_ops operator class for access method ivfflat.
 */
Oid
VectorIVFFlatL2SimilarityOperatorFamilyId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorIVFFlatL2SimilarityOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.VectorIVFFlatL2SimilarityOperatorFamilyId = get_opfamily_oid(
			PgVectorIvfFlatIndexAmId(), list_make2(makeString("public"), makeString(
													   "vector_l2_ops")),
			missingOk);
	}

	return Cache.VectorIVFFlatL2SimilarityOperatorFamilyId;
}


/*
 * VectorHNSWL2SimilarityOperatorFamilyId returns
 * the OID of the vector_l2_ops operator class for access method hnsw.
 */
Oid
VectorHNSWL2SimilarityOperatorFamilyId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorHNSWL2SimilarityOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.VectorHNSWL2SimilarityOperatorFamilyId = get_opfamily_oid(
			PgVectorHNSWIndexAmId(), list_make2(makeString("public"), makeString(
													"vector_l2_ops")),
			missingOk);
	}

	return Cache.VectorHNSWL2SimilarityOperatorFamilyId;
}


/*
 * VectorIVFFlatIPSimilarityOperatorFamilyId returns
 * the OID of the vector_ip_ops operator class for access method ivfflat.
 */
Oid
VectorIVFFlatIPSimilarityOperatorFamilyId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorIVFFlatIPSimilarityOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.VectorIVFFlatIPSimilarityOperatorFamilyId = get_opfamily_oid(
			PgVectorIvfFlatIndexAmId(), list_make2(makeString("public"), makeString(
													   "vector_ip_ops")),
			missingOk);
	}

	return Cache.VectorIVFFlatIPSimilarityOperatorFamilyId;
}


/*
 * VectorHNSWIPSimilarityOperatorFamilyId returns
 * the OID of the vector_ip_ops operator class for access method hnsw.
 */
Oid
VectorHNSWIPSimilarityOperatorFamilyId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.VectorHNSWIPSimilarityOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.VectorHNSWIPSimilarityOperatorFamilyId = get_opfamily_oid(
			PgVectorHNSWIndexAmId(), list_make2(makeString("public"), makeString(
													"vector_ip_ops")),
			missingOk);
	}

	return Cache.VectorHNSWIPSimilarityOperatorFamilyId;
}


/*
 * Returns the OID of gin_bson_exclusion_pre_consistent function.
 * Note: This and the associated call can be removed once 1.11 rolls out.
 */
Oid
BsonExclusionPreConsistentFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonExclusionPreconsistentFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString(
												"gin_bson_exclusion_pre_consistent"));

		bool missingOK = false;
		ObjectWithArgs args = { 0 };
		args.args_unspecified = true;
		args.objname = functionNameList;

		Cache.BsonExclusionPreconsistentFunctionId = LookupFuncWithArgs(OBJECT_FUNCTION,
																		&args, missingOK);
	}

	return Cache.BsonExclusionPreconsistentFunctionId;
}


/*
 * BsonGreaterThanOperatorId returns the OID of the <bson> > <bson> operator.
 */
Oid
BsonGreaterThanOperatorId(void)
{
	return GetCoreBinaryOperatorId(&Cache.BsonGreaterThanOperatorId,
								   BsonTypeId(), ">", BsonTypeId());
}


Oid
BsonGreaterThanEqualOperatorId(void)
{
	return GetCoreBinaryOperatorId(&Cache.BsonGreaterThanEqualOperatorId,
								   BsonTypeId(), ">=", BsonTypeId());
}


Oid
BsonLessThanEqualOperatorId(void)
{
	return GetCoreBinaryOperatorId(&Cache.BsonLessThanEqualOperatorId,
								   BsonTypeId(), "<=", BsonTypeId());
}


/*
 * BsonLessThanOperatorId returns the OID of the <bson> < <bson> operator.
 */
Oid
BsonLessThanOperatorId(void)
{
	return GetCoreBinaryOperatorId(&Cache.BsonLessThanOperatorId,
								   BsonTypeId(), "<", BsonTypeId());
}


/*
 * OID of the operator class for BSON Text operations with {ExtensionObjectPrefix}_rum
 */
Oid
BsonRumTextPathOperatorFamily(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonRumTextPathOperatorFamily == InvalidOid)
	{
		/* Handles extension version upgrades */
		bool missingOk = true;
		Oid rumAmId = RumIndexAmId();
		Cache.BsonRumTextPathOperatorFamily = get_opfamily_oid(
			rumAmId, list_make2(makeString(ApiCatalogSchemaName), makeString(
									"bson_rum_text_path_ops")),
			missingOk);
	}

	return Cache.BsonRumTextPathOperatorFamily;
}


/*
 * OID of the operator class for BSON GIST spherical geometries
 */
Oid
BsonGistGeographyOperatorFamily(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonGistGeographyOperatorFamily == InvalidOid)
	{
		/* Handles extension version upgrades */
		bool missingOk = false;
		Oid amId = GIST_AM_OID;
		Cache.BsonGistGeographyOperatorFamily = get_opfamily_oid(
			amId, list_make2(makeString(ApiCatalogSchemaName), makeString(
								 "bson_gist_geography_ops_2d")),
			missingOk);
	}

	return Cache.BsonGistGeographyOperatorFamily;
}


/*
 * OID of the operator class for BSON GIST spherical geometries
 */
Oid
BsonGistGeometryOperatorFamily(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonGistGeometryOperatorFamily == InvalidOid)
	{
		/* Handles extension version upgrades */
		bool missingOk = false;
		Oid amId = GIST_AM_OID;
		Cache.BsonGistGeometryOperatorFamily = get_opfamily_oid(
			amId, list_make2(makeString(ApiCatalogSchemaName), makeString(
								 "bson_gist_geometry_ops_2d")),
			missingOk);
	}

	return Cache.BsonGistGeometryOperatorFamily;
}


/*
 * OID of the operator class for BSON Single Path operations with {ExtensionObjectPrefix}_rum
 */
Oid
BsonRumSinglePathOperatorFamily(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonRumSinglePathOperatorFamily == InvalidOid)
	{
		/* Handles extension version upgrades */
		bool missingOk = false;
		Oid rumAmId = RumIndexAmId();
		Cache.BsonRumSinglePathOperatorFamily = get_opfamily_oid(
			rumAmId, list_make2(makeString(ApiCatalogSchemaName), makeString(
									"bson_rum_single_path_ops")),
			missingOk);
	}

	return Cache.BsonRumSinglePathOperatorFamily;
}


/*
 * Returns the OID of the pg_catalog.websearch_to_tsquery function that takes
 * a single web search query text.
 */
Oid
WebSearchToTsQueryFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.WebSearchToTsQueryFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("pg_catalog"),
											makeString("websearch_to_tsquery"));
		Oid paramOids[1] = { TEXTOID };
		bool missingOK = false;

		Cache.WebSearchToTsQueryFunctionId =
			LookupFuncName(functionNameList, 1, paramOids, missingOK);
	}

	return Cache.WebSearchToTsQueryFunctionId;
}


/*
 * Returns the OID of the pg_catalog.websearch_to_tsquery function that
 * takes a web search query text and a text-search dictionary configuration.
 */
Oid
WebSearchToTsQueryWithRegConfigFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.WebSearchToTsQueryWithRegConfigFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("pg_catalog"),
											makeString("websearch_to_tsquery"));
		Oid paramOids[2] = { REGCONFIGOID, TEXTOID };
		bool missingOK = false;

		Cache.WebSearchToTsQueryWithRegConfigFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.WebSearchToTsQueryWithRegConfigFunctionId;
}


/*
 * Returns the OID of the extract_tsvector function that the RUM extension
 * has for the default TSVector operator class
 */
Oid
RumExtractTsVectorFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.RumExtractTsVectorFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(RUM_EXTENSION_SCHEMA),
											makeString("rum_extract_tsvector"));
		Oid paramOids[5] = {
			TSVECTOROID, INTERNALOID, INTERNALOID, INTERNALOID, INTERNALOID
		};
		bool missingOK = false;
		Cache.RumExtractTsVectorFunctionId =
			LookupFuncName(functionNameList, 5, paramOids, missingOK);
	}

	return Cache.RumExtractTsVectorFunctionId;
}


/*
 * Returns the OID of the bson_text_meta_qual function ID
 */
Oid
BsonTextSearchMetaQualFuncId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonTextSearchMetaQualFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogToApiInternalSchemaName),
											makeString("bson_text_meta_qual"));
		Oid paramOids[4] = { BsonTypeId(), TSQUERYOID, BYTEAOID, BOOLOID };
		bool missingOK = false;
		Cache.BsonTextSearchMetaQualFuncId =
			LookupFuncName(functionNameList, 4, paramOids, missingOK);
	}

	return Cache.BsonTextSearchMetaQualFuncId;
}


Oid
TsRankFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.PostgresTsRankFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("pg_catalog"),
											makeString("ts_rank_cd"));
		Oid paramOids[3] = { FLOAT4ARRAYOID, TSVECTOROID, TSQUERYOID };
		bool missingOK = false;
		Cache.PostgresTsRankFunctionId =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return Cache.PostgresTsRankFunctionId;
}


Oid
TsVectorConcatFunctionId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.TsVectorConcatFunctionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("pg_catalog"),
											makeString("tsvector_concat"));
		Oid paramOids[2] = { TSVECTOROID, TSVECTOROID };
		bool missingOK = false;
		Cache.TsVectorConcatFunctionId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.TsVectorConcatFunctionId;
}


/*
 * Returns the OID of the ts_match_vq function (maps to the function of
 * the tsvector @@ tsquery operator).
 */
Oid
TsMatchFunctionOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.TsMatchFunctionOid == InvalidOid)
	{
		List *functionNameList = list_make2(makeString("pg_catalog"),
											makeString("ts_match_vq"));
		Oid paramOids[2] = { TSVECTOROID, TSQUERYOID };
		bool missingOK = false;
		Cache.TsMatchFunctionOid =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return Cache.TsMatchFunctionOid;
}


/*
 * GetBinaryOperatorId is a helper function for getting and caching the OID
 * of a <leftTypeOid> <operatorName> <rightTypeOid> operator.
 */
static Oid
GetBinaryOperatorId(Oid *operatorId, Oid leftTypeOid, char *operatorName,
					Oid rightTypeOid)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString(operatorName));

		*operatorId =
			OpernameGetOprid(operatorNameList, leftTypeOid, rightTypeOid);
	}

	return *operatorId;
}


static Oid
GetInternalBinaryOperatorId(Oid *operatorId, Oid leftTypeOid, char *operatorName,
							Oid rightTypeOid)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString(ApiInternalSchemaNameV2),
											makeString(operatorName));

		*operatorId =
			OpernameGetOprid(operatorNameList, leftTypeOid, rightTypeOid);
	}

	return *operatorId;
}


/*
 * Gets the BinaryOperatorId similar to the function above, except in the CORE schema
 * and not the API catalog schema.
 */
static Oid
GetCoreBinaryOperatorId(Oid *operatorId, Oid leftTypeOid, char *operatorName,
						Oid rightTypeOid)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorId == InvalidOid)
	{
		List *operatorNameList = list_make2(makeString(CoreSchemaName),
											makeString(operatorName));

		*operatorId =
			OpernameGetOprid(operatorNameList, leftTypeOid, rightTypeOid);
	}

	return *operatorId;
}


/*
 * GetBinaryOperatorFunctionId is a helper function for getting and caching the OID
 * of a <functionName> <leftTypeOid> <rightTypeOid> operator.
 * These are binary operators where we may need to handle "missingOk" scenarios.
 * The releaseName tracks the release that introduced the operator is
 * there. This is needed until the PITR window for that release has passed.
 */
static Oid
GetBinaryOperatorFunctionIdMissingOk(Oid *operatorFuncId, char *operatorName,
									 Oid leftTypeOid, Oid rightTypeOid,
									 const char *releaseName)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString(operatorName));
		Oid paramOids[2] = { leftTypeOid, rightTypeOid };
		bool missingOK = true;

		*operatorFuncId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return *operatorFuncId;
}


/*
 * GetBinaryOperatorFunctionId is a helper function for getting and caching the OID
 * of a <functionName> <leftTypeOid> <rightTypeOid> operator.
 */
static Oid
GetInternalBinaryOperatorFunctionId(Oid *operatorFuncId, char *operatorName,
									Oid leftTypeOid, Oid rightTypeOid)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(ApiCatalogToApiInternalSchemaName),
											makeString(operatorName));
		Oid paramOids[2] = { leftTypeOid, rightTypeOid };
		bool missingOK = false;

		*operatorFuncId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return *operatorFuncId;
}


/*
 * GetBinaryOperatorFunctionIdWithSchema is a helper function for getting and caching the OID
 * of a <functionName> <leftTypeOid> <rightTypeOid> operator.
 */
static Oid
GetBinaryOperatorFunctionIdWithSchema(Oid *operatorFuncId, char *operatorName,
									  Oid leftTypeOid, Oid rightTypeOid,
									  char *schemaName)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(schemaName),
											makeString(operatorName));
		Oid paramOids[2] = { leftTypeOid, rightTypeOid };
		bool missingOK = false;

		*operatorFuncId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return *operatorFuncId;
}


/*
 * GetBinaryOperatorFunctionId is a helper function for getting and caching the OID
 * of a <functionName> <leftTypeOid> <rightTypeOid> operator.
 */
static Oid
GetBinaryOperatorFunctionId(Oid *operatorFuncId, char *operatorName,
							Oid leftTypeOid, Oid rightTypeOid)
{
	return GetBinaryOperatorFunctionIdWithSchema(operatorFuncId, operatorName,
												 leftTypeOid, rightTypeOid,
												 ApiCatalogSchemaName);
}


static Oid
GetOperatorFunctionIdThreeArgs(Oid *operatorFuncId, char *schemaName, char *operatorName,
							   Oid arg0TypeOid, Oid arg1TypeOid, Oid arg2TypeOid)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(schemaName),
											makeString(operatorName));
		Oid paramOids[3] = { arg0TypeOid, arg1TypeOid, arg2TypeOid };
		bool missingOK = false;

		*operatorFuncId =
			LookupFuncName(functionNameList, 3, paramOids, missingOK);
	}

	return *operatorFuncId;
}


static Oid
GetOperatorFunctionIdFourArgs(Oid *operatorFuncId, char *schemaName, char *operatorName,
							  Oid arg0TypeOid, Oid arg1TypeOid, Oid arg2TypeOid,
							  Oid arg3TypeOid)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(schemaName),
											makeString(operatorName));
		Oid paramOids[4] = { arg0TypeOid, arg1TypeOid, arg2TypeOid, arg3TypeOid };
		bool missingOK = false;

		*operatorFuncId =
			LookupFuncName(functionNameList, 4, paramOids, missingOK);
	}

	return *operatorFuncId;
}


static Oid
GetOperatorFunctionIdFiveArgs(Oid *operatorFuncId, char *schemaName, char *operatorName,
							  Oid arg0TypeOid, Oid arg1TypeOid, Oid arg2TypeOid,
							  Oid arg3TypeOid, Oid arg4TypeId)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(schemaName),
											makeString(operatorName));
		Oid paramOids[5] = {
			arg0TypeOid, arg1TypeOid, arg2TypeOid, arg3TypeOid, arg4TypeId
		};
		bool missingOK = false;

		*operatorFuncId =
			LookupFuncName(functionNameList, 5, paramOids, missingOK);
	}

	return *operatorFuncId;
}


static Oid
GetDocumentDBInternalBinaryOperatorFunctionId(Oid *operatorFuncId, char *operatorName,
											  Oid leftTypeOid, Oid rightTypeOid,
											  bool missingOK)
{
	InitializeDocumentDBApiExtensionCache();

	if (*operatorFuncId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(DocumentDBApiInternalSchemaName),
											makeString(operatorName));
		Oid paramOids[2] = { leftTypeOid, rightTypeOid };

		*operatorFuncId =
			LookupFuncName(functionNameList, 2, paramOids, missingOK);
	}

	return *operatorFuncId;
}


/*
 * GetPostgresInternalFunctionId is a helper function for getting and caching the OID
 * of a postgres internal method
 */
Oid
GetPostgresInternalFunctionId(Oid *funcId, char *operatorName)
{
	InitializeDocumentDBApiExtensionCache();

	if (*funcId == InvalidOid)
	{
		*funcId = fmgr_internal_function(operatorName);
	}

	return *funcId;
}


Oid
ApiCatalogCollectionIdSequenceId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CollectionIdSequenceId == InvalidOid)
	{
		List *sequenceNameList = list_make2(makeString(ApiCatalogSchemaName),
											makeString("collections_collection_id_seq"));
		RangeVar *sequenceRelRangeVar = makeRangeVarFromNameList(sequenceNameList);

		/* use AccessShareLock to prevent it getting dropped concurrently */
		bool missingOk = false;
		Cache.CollectionIdSequenceId =
			RangeVarGetRelid(sequenceRelRangeVar, AccessShareLock, missingOk);
	}

	return Cache.CollectionIdSequenceId;
}


Oid
ApiCatalogCollectionIndexIdSequenceId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.CollectionIndexIdSequenceId == InvalidOid)
	{
		List *sequenceNameList =
			list_make2(makeString(ApiCatalogSchemaName),
					   makeString("collection_indexes_index_id_seq"));
		RangeVar *sequenceRelRangeVar = makeRangeVarFromNameList(sequenceNameList);

		/* use AccessShareLock to prevent it getting dropped concurrently */
		bool missingOk = false;
		Cache.CollectionIndexIdSequenceId =
			RangeVarGetRelid(sequenceRelRangeVar, AccessShareLock, missingOk);
	}

	return Cache.CollectionIndexIdSequenceId;
}


/*
 * Helper utility for getting Oid of a function name with given number of
 * args and schema name.
 */
static Oid
GetSchemaFunctionIdWithNargs(Oid *functionId, char *schema,
							 char *functionName, int nargs,
							 Oid *argTypes, bool missingOk)
{
	InitializeDocumentDBApiExtensionCache();

	if (*functionId == InvalidOid)
	{
		List *functionNameList = list_make2(makeString(schema),
											makeString(functionName));
		*functionId =
			LookupFuncName(functionNameList, nargs, argTypes, missingOk);
	}

	return *functionId;
}


/*
 * Given Oid of the base element type, return the Oid of the array type.
 */
static Oid
GetArrayTypeOid(Oid *arrayTypeId, Oid baseElementType)
{
	InitializeDocumentDBApiExtensionCache();

	if (*arrayTypeId == InvalidOid)
	{
		*arrayTypeId = get_array_type(baseElementType);
	}

	return *arrayTypeId;
}


/*
 * Wrapper function that checks for cluster version before deciding to return
 * a BsonQueryTypeid or BsonTypeId. TODO - Delete post v1.11.
 */
Oid
GetClusterBsonQueryTypeId()
{
	Oid typeId = BsonQueryTypeId();
	if (typeId == InvalidOid)
	{
		return BsonTypeId();
	}

	return typeId;
}


/*
 * Returns the OID of the ApiInternalSchemaName.index_build_is_in_progress function.
 */
Oid
IndexBuildIsInProgressFunctionId()
{
	int nargs = 1;
	Oid argTypes[1] = { INT4OID };
	bool missingOk = false;
	return GetSchemaFunctionIdWithNargs(&Cache.IndexBuildIsInProgressFunctionId,
										ApiInternalSchemaName,
										"index_build_is_in_progress", nargs, argTypes,
										missingOk);
}


/*
 * Returns the OID of the ApiDataSchemaName namespace
 */
Oid
ApiDataNamespaceOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.ApiDataNamespaceOid == InvalidOid)
	{
		bool missingOk = false;
		Cache.ApiDataNamespaceOid = get_namespace_oid(ApiDataSchemaName, missingOk);
	}

	return Cache.ApiDataNamespaceOid;
}


/*
 * Returns Oid of array type for bson
 */
Oid
GetBsonArrayTypeOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonArrayTypeOid == InvalidOid)
	{
		Cache.BsonArrayTypeOid = get_array_type(BsonTypeId());
	}

	return Cache.BsonArrayTypeOid;
}


Oid
BsonIndexBoundsTypeId(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonIndexBoundsTypeOid == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString(ApiInternalSchemaNameV2),
											makeString("bsonindexbounds"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		Cache.BsonIndexBoundsTypeOid = typenameTypeId(NULL, bsonTypeName);
	}

	return Cache.BsonIndexBoundsTypeOid;
}


Oid
GetBsonIndexBoundsArrayTypeOid(void)
{
	InitializeDocumentDBApiExtensionCache();

	if (Cache.BsonIndexBoundsArrayTypeOid == InvalidOid)
	{
		Cache.BsonIndexBoundsArrayTypeOid = get_array_type(BsonIndexBoundsTypeId());
	}

	return Cache.BsonIndexBoundsArrayTypeOid;
}


char *
GetExtensionApplicationName(void)
{
	return psprintf("%s-Internal", ExtensionObjectPrefixV2);
}
