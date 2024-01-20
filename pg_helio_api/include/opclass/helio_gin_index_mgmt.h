/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/helio_gin_index_mgmt.h
 *
 * Common declarations of the bson index management methods.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_GIN_INDEX_MGMT_H
#define HELIO_GIN_INDEX_MGMT_H

/*
 * Enum identifying the kind of index based on the options.
 */
typedef enum IndexOptionsType
{
	/* This is an index of a single path - wildcard or not */
	IndexOptionsType_SinglePath,

	/* This is a wildcard index with multiple path projections */
	IndexOptionsType_Wildcard,

	/* This is a hash index on a single path */
	IndexOptionsType_Hashed,

	/* This is the unique shard_key hash index */
	IndexOptionsType_UniqueShardKey,

	/* This is a text index on a single path */
	IndexOptionsType_Text,

	/* This is a 2d index on a single path */
	IndexOptionsType_2d,

	/* This is a 2dsphere index on a path */
	IndexOptionsType_2dsphere,
} IndexOptionsType;


/*
 * Versioning for indexes when making breaking changes to index layout/terms.
 */
typedef enum IndexOptionsVersion
{
	/* The default value (version that got started with) */
	IndexOptionsVersion_V0 = 0,

	/* Version that includes term path prefix elision for single path indexes & index term truncation */
	IndexOptionsVersion_V1 = 1,
} IndexOptionsVersion;


/*
 * Base struct for all options used in the GIN index.
 * Note - all option classes must have the same layout.
 */
typedef struct
{
	int32 vl_len_;              /* varlena header (do not touch directly!) */
	IndexOptionsType type;      /* This must be the first field */
	IndexOptionsVersion version;
	int intOption_deprecated;   /* This is a deprecated field for int type fields */
	int32_t indexTermTruncateLimit; /* this must be the next field in all index options */
} BsonGinIndexOptionsBase;

/*
 * This is the serialized post-processed structure that holds the indexing options
 * for single path indexes (both wildcard and non-wildcard)
 * This handles indexes of the form { "$**": 1 }, { "a.b.$**" : 1 }
 * { "a.b" : 1 }, { "a": 1 } etc.
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	bool isWildcard;
	bool generateNotFoundTerm;
	int path;
} BsonGinSinglePathOptions;

/*
 * This is the serialized post-processed structure that holds the indexing options
 * for wildcard projection path indexes
 * This handles indexes of the form
 * { "$**": 1, { "wildcardProjection": { "a.b": 1, "d": 1 } } }
 * { "$**": 1, { "wildcardProjection": { "a.b": 0, "_id": 1 } } }
 * Holds information on whether it's an inclusion/exclusion index and the set of paths
 * to be considered for indexing.
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	bool isExclusion;
	bool includeId;
	int pathSpec;
} BsonGinWildcardProjectionPathOptions;

/*
 * This is the serialized post-processed structure that holds the indexing options
 * for hashed indexes
 * This handles indexes of the form { "a": "hashed" }, { "a.b" : "hashed" }
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	int path;
} BsonGinHashOptions;


/*
 * This is the serialized post-processed structure that holds the indexing options
 * for single path exclusion hash indexes.
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	int path;
} BsonGinExclusionHashOptions;

/*
 * This is the serialized post-processed structure that holds the indexing options
 * for single path text indexes (both wildcard and non-wildcard)
 * This handles indexes of the form { "$**": "text" },
 * { "a.b" : "text" }, { "a": "text" } etc.
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	bool isWildcard;
	int defaultLanguage;
	int weights;
	int languageOverride;
} BsonGinTextPathOptions;


/*
 * This is the serialized post-processed structure that holds the indexing options
 * for single path 2d indexes.
 *
 * This handles indexes of the form { "a": "2d" }, { "a.b" : "2d" }
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	int path;
	double maxBound;
	double minBound;
} Bson2dGeometryPathOptions;

/*
 * This is the serialized post-processed structure that holds the indexing options
 * for single path 2dsphere indexes.
 */
typedef struct
{
	BsonGinIndexOptionsBase base;
	int path;
} Bson2dGeographyPathOptions;

bool ValidateIndexForQualifierValue(bytea *indexOptions, Datum queryValue,
									BsonIndexStrategy
									strategy);

Size FillSinglePathSpec(const char *prefix, void *buffer);
void ValidateSinglePathSpec(const char *prefix);
Size FillDeprecatedStringSpec(const char *value, void *ptr);

/* Helper macro to retrieve a length prefixed value in the index options */
#define Get_Index_Path_Option(options, field, result, resultFieldLength) \
	const char *pathDefinition = GET_STRING_RELOPTION(options, field); \
	if (pathDefinition == NULL) { resultFieldLength = 0; result = NULL; } \
	else { resultFieldLength = *(uint32_t *) pathDefinition; result = pathDefinition + \
																	  sizeof(uint32_t); }

#endif
