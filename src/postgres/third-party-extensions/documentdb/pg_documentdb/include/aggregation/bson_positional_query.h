/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_positional_query.h
 *
 * Declarations of functions for the BSON Positional $ operator.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_POSITIONAL_QUERY_H
#define BSON_POSITIONAL_QUERY_H

typedef struct BsonPositionalQueryData BsonPositionalQueryData;

/*
 * A list of BsonPositionalQueryQual for each
 * qualifier found within a top level query.
 */
typedef struct BsonPositionalQueryData
{
	List *queryQuals;
} BsonPositionalQueryData;

BsonPositionalQueryData * GetPositionalQueryData(const pgbson *query);

int32_t MatchPositionalQueryAgainstDocument(const BsonPositionalQueryData *data,
											const pgbson *document);

#endif
