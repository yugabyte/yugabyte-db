/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/metadata_cache.h
 *
 * Common declarations for metadata caching functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef METADATA_CACHE_H
#define METADATA_CACHE_H


extern char *ApiDataSchemaName;
extern char *ApiAdminRole;
extern char *ApiSchemaName;
extern char *ApiInternalSchemaName;
extern char *ExtensionObjectPrefix;
extern char *CoreSchemaName;
extern char *FullBsonTypeName;
extern char *ApiCatalogSchemaName;

extern MemoryContext HelioApiMetadataCacheContext;

/* functions related with pg_helio_api "extension" itself */
void InitializeHelioApiExtensionCache(void);
void InvalidateCollectionsCache(void);
bool IsHelioApiExtensionActive(void);
Oid HelioApiExtensionOwner(void);

/* functions and procedures */
Oid ApiCollectionFunctionId(void);
Oid ApiCreateIndexesProcedureId(void);
Oid ApiReIndexProcedureId(void);
Oid BsonEqualMatchRuntimeFunctionId(void);
Oid BsonEqualMatchRuntimeOperatorId(void);
Oid BsonEqualMatchIndexFunctionId(void);
Oid BsonGreaterThanMatchRuntimeFunctionId(void);
Oid BsonGreaterThanMatchRuntimeOperatorId(void);
Oid BsonGreaterThanMatchIndexFunctionId(void);
Oid BsonGreaterThanEqualMatchRuntimeFunctionId(void);
Oid BsonGreaterThanEqualMatchRuntimeOperatorId(void);
Oid BsonGreaterThanEqualMatchIndexFunctionId(void);
Oid BsonLessThanMatchRuntimeFunctionId(void);
Oid BsonLessThanMatchRuntimeOperatorId(void);
Oid BsonLessThanMatchIndexFunctionId(void);
Oid BsonLessThanEqualMatchRuntimeFunctionId(void);
Oid BsonLessThanEqualMatchRuntimeOperatorId(void);
Oid BsonLessThanEqualMatchIndexFunctionId(void);
Oid BsonRangeMatchFunctionId(void);
Oid BsonRangeMatchOperatorOid(void);
Oid BsonInMatchFunctionId(void);
Oid BsonNinMatchFunctionId(void);
Oid BsonNotEqualMatchFunctionId(void);
Oid BsonElemMatchMatchFunctionId(void);
Oid BsonAllMatchFunctionId(void);
Oid BsonBitsAllClearFunctionId(void);
Oid BsonBitsAnyClearFunctionId(void);
Oid BsonBitsAllSetFunctionId(void);
Oid BsonBitsAnySetFunctionId(void);
Oid BsonRegexMatchFunctionId(void);
Oid BsonModMatchFunctionId(void);
Oid BsonSizeMatchFunctionId(void);
Oid BsonTypeMatchFunctionId(void);
Oid BsonExistsMatchFunctionId(void);
Oid BsonExprFunctionId(void);
Oid BsonTextFunctionId(void);
Oid BsonEmptyDataTableFunctionId(void);
Oid IndexSpecAsBsonFunctionId(void);
Oid IndexBuildIsInProgressFunctionId(void);
Oid ApiCursorStateFunctionId(void);
Oid ApiCurrentCursorStateFunctionId(void);
Oid ApiSampleCollectionFunctionId(void);
Oid ExtensionTableSampleSystemRowsFunctionId(void);

/* bson_value functions */
Oid BsonValueEqualMatchFunctionId(void);
Oid BsonValueGreaterThanMatchFunctionId(void);
Oid BsonValueGreaterThanEqualMatchFunctionId(void);
Oid BsonValueLessThanMatchFunctionId(void);
Oid BsonValueLessThanEqualMatchFunctionId(void);
Oid BsonValueSizeMatchFunctionId(void);
Oid BsonValueTypeMatchFunctionId(void);
Oid BsonValueInMatchFunctionId(void);
Oid BsonValueNinMatchFunctionId(void);
Oid BsonValueNotEqualMatchFunctionId(void);
Oid BsonValueExistsMatchFunctionId(void);
Oid BsonValueElemMatchMatchFunctionId(void);
Oid BsonValueAllMatchFunctionId(void);
Oid BsonValueRegexMatchFunctionId(void);
Oid BsonValueModMatchFunctionId(void);
Oid BsonValueBitsAllClearFunctionId(void);
Oid BsonValueBitsAnyClearFunctionId(void);
Oid BsonValueBitsAllSetFunctionId(void);
Oid BsonValueBitsAnySetFunctionId(void);

/* operators */
Oid BigintEqualOperatorId(void);
Oid TextEqualOperatorId(void);
Oid BsonArrowOperatorId(void);
Oid BsonEqualOperatorId(void);
Oid BsonEqualMatchOperatorId(void);
Oid BsonQueryOperatorId(void);
Oid BsonTrueFunctionId(void);
Oid BsonQueryMatchFunctionId(void);
Oid BsonIndexSpecSelectionFunctionId(void);
Oid BsonIndexSpecEqualOperatorId(void);
Oid BsonGreaterThanOperatorId(void);
Oid BsonLessThanOperatorId(void);
Oid BsonGreaterThanEqualOperatorId(void);
Oid BsonLessThanEqualOperatorId(void);

/* types */
Oid BsonTypeId(void);
Oid BsonQueryTypeId(void);
Oid VectorTypeId(void);
Oid IndexSpecTypeId(void);
Oid MongoCatalogCollectionsTypeOid(void);
Oid GetClusterBsonQueryTypeId(void);

/* sequences */
Oid ApiCatalogCollectionIdSequenceId(void);
Oid ApiCatalogCollectionIndexIdSequenceId(void);

/* order by */
Oid BsonOrderByQueryOperatorId(void);
Oid BsonOrderByFunctionOid(void);

/* Postgres internal functions */
Oid PostgresDrandomFunctionId(void);
Oid PostgresToTimestamptzFunctionId(void);
Oid PostgresDatePartFunctionId(void);
Oid PostgresTimestampToZoneFunctionId(void);

/* Index AM */
Oid RumIndexAmId(void);
Oid PgVectorIvfFlatIndexAmId(void);
Oid PgVectorHNSWIndexAmId(void);

/* IndexAM Support functions */
Oid BsonExclusionPreConsistentFunctionId(void);

/* Operator Class*/
Oid VectorIVFFlatCosineSimilarityOperatorFamilyId(void);
Oid VectorIVFFlatIPSimilarityOperatorFamilyId(void);
Oid VectorIVFFlatL2SimilarityOperatorFamilyId(void);
Oid VectorHNSWCosineSimilarityOperatorFamilyId(void);
Oid VectorHNSWIPSimilarityOperatorFamilyId(void);
Oid VectorHNSWL2SimilarityOperatorFamilyId(void);
Oid BsonRumTextPathOperatorFamily(void);
Oid BsonRumSinglePathOperatorFamily(void);

/* Vector Functions */
Oid PgDoubleToVectorFunctionOid(void);
Oid VectorAsVectorFunctionOid(void);
Oid ApiCatalogBsonExtractVectorFunctionId(void);
Oid ApiBsonSearchParamFunctionId(void);

/* Vector Operators */
Oid VectorOrderByQueryOperatorId(void);
Oid VectorCosineSimilaritySearchOperatorId(void);
Oid VectorL2SimilaritySearchOperatorId(void);
Oid VectorIPSimilaritySearchOperatorId(void);

/* Geospatial data/type/support functions */
Oid Box2dfTypeId(void);
Oid GeometryTypeId(void);
Oid GeographyTypeId(void);
Oid GIDXTypeId(void);
Oid GeometryArrayTypeId(void);
Oid BsonDollarGeoIntersectsFunctionOid(void);
Oid BsonDollarGeowithinFunctionOid(void);
Oid BsonValidateGeometryFunctionId(void);
Oid BsonValidateGeographyFunctionId(void);
Oid PostgisBufferFunctionId(void);
Oid PostgisGeometryAreaFunctionId(void);
Oid PostgisGeometryFromEWKBFunctionId(void);
Oid PostgisGeometryAsGeography(void);
Oid PostgisGeometryIsValidDetailFunctionId(void);
Oid PostgisGeometryGistCompress2dFunctionId(void);
Oid PostgisGeometryGistConsistent2dFunctionId(void);
Oid PostgisGeographyFromWKBFunctionId(void);
Oid PostgisGeographyGistCompressFunctionId(void);
Oid PostgisGeographyGistConsistentFunctionId(void);
Oid PostgisMakeEnvelopeFunctionId(void);
Oid PostgisMakePointFunctionId(void);
Oid PostgisMakePolygonFunctionId(void);
Oid PostgisMakeLineFunctionId(void);
Oid PostgisGeographyCoversFunctionId(void);
Oid PostgisBox2dfGeometryOverlapsFunctionId(void);
Oid PostgisGIDXGeographyOverlapsFunctionId(void);
Oid PostgisGeometryCoversFunctionId(void);
Oid PostgisGeographyIntersectsFunctionId(void);
Oid PostgisSetSRIDFunctionId(void);

/* Text search functions */
Oid WebSearchToTsQueryFunctionId(void);
Oid WebSearchToTsQueryWithRegConfigFunctionId(void);
Oid RumExtractTsVectorFunctionId(void);
Oid BsonTextSearchMetaQualFuncId(void);
Oid TsRankFunctionId(void);
Oid TsVectorConcatFunctionId(void);
Oid TsMatchFunctionOid(void);


/* Aggregation functions */
Oid ApiCatalogAggregationPipelineFunctionId(void);
Oid ApiCatalogAggregationFindFunctionId(void);
Oid ApiCatalogAggregationCountFunctionId(void);
Oid ApiCatalogAggregationDistinctFunctionId(void);
Oid BsonDollarAddFieldsFunctionOid(void);
Oid BsonDollarProjectFunctionOid(void);
Oid BsonDollarProjectFindFunctionOid(void);
Oid BsonDollarUnwindFunctionOid(void);
Oid BsonDollarUnwindWithOptionsFunctionOid(void);
Oid BsonDollarReplaceRootFunctionOid(void);
Oid BsonSumAggregateFunctionOid(void);
Oid BsonAvgAggregateFunctionOid(void);
Oid BsonRepathAndBuildFunctionOid(void);
Oid BsonExpressionGetFunctionOid(void);
Oid BsonExpressionMapFunctionOid(void);
Oid BsonMaxAggregateFunctionOid(void);
Oid BsonMinAggregateFunctionOid(void);
Oid PgRandomFunctionOid(void);
Oid BsonArrayAggregateFunctionOid(void);
Oid BsonObjectAggregateFunctionOid(void);
Oid BsonDollarFacetProjectFunctionOid(void);
Oid BsonFirstOnSortedAggregateFunctionOid(void);
Oid BsonLastOnSortedAggregateFunctionOid(void);
Oid BsonFirstAggregateFunctionOid(void);
Oid BsonLastAggregateFunctionOid(void);
Oid BsonFirstNAggregateFunctionOid(void);
Oid BsonFirstNOnSortedAggregateFunctionOid(void);
Oid BsonLastNAggregateFunctionOid(void);
Oid BsonLastNOnSortedAggregateFunctionOid(void);
Oid BsonLookupExtractFilterExpressionFunctionOid(void);
Oid BsonLookupUnwindFunctionOid(void);
Oid BsonDistinctUnwindFunctionOid(void);
Oid BsonDistinctAggregateFunctionOid(void);
Oid RowGetBsonFunctionOid(void);
Oid ApiCollStatsAggregationFunctionOid(void);
Oid ApiIndexStatsAggregationFunctionOid(void);
Oid BsonCurrentOpAggregationFunctionId(void);


/* Catalog */
Oid ApiDataNamespaceOid(void);

#endif
