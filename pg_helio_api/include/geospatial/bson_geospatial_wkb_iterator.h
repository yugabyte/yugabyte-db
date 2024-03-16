/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_wkb_iterator.h
 *
 * Custom iterator for WKB buffer
 *
 *-------------------------------------------------------------------------
 */
#ifndef WKBBufferITERATOR_H
#define WKBBUfferITERATOR_H

#include "utils/mongo_errors.h"

typedef struct WKBBufferIterator
{
	/* Pointer to start of wkb buffer */
	const char *headptr;

	/* Pointer to current position in buffer while iterating */
	char *currptr;

	/* Original length of buffer */
	int len;
}WKBBufferIterator;

/* Initialize a WKBBufferIterator from given wkb buffer stringinfo */
static inline void
InitIteratorFromWKBBuffer(WKBBufferIterator *iter, StringInfo wkbBuffer)
{
	iter->headptr = wkbBuffer->data;

	/* Set currptr also to wkbBuffer->data as we start from the head */
	iter->currptr = wkbBuffer->data;

	iter->len = wkbBuffer->len;
}


/* Util to increment current pointer in iterator by given number of bytes */
static inline void
IncrementWKBBufferIteratorByNBytes(WKBBufferIterator *iter, size_t bytes)
{
	size_t remainingLength = (size_t) iter->len - (iter->currptr - iter->headptr);
	if (remainingLength >= bytes)
	{
		iter->currptr += bytes;
	}
	else
	{
		size_t overflow = bytes - remainingLength;
		ereport(ERROR, (
					errcode(MongoInternalError),
					errmsg(
						"Requested to increment WKB buffer %ld bytes beyond limit.",
						overflow),
					errhint(
						"Requested to increment WKB buffer %ld bytes beyond limit.",
						overflow)));
	}
}


#endif
