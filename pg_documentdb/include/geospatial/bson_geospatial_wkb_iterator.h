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
#define WKBBufferITERATOR_H

#include "postgres.h"

#include "geospatial/bson_geospatial_private.h"
#include "utils/helio_errors.h"

typedef struct WKBBufferIterator
{
	/* Pointer to start of wkb buffer */
	const char *headptr;

	/* Pointer to current position in buffer while iterating */
	char *currptr;

	/* Original length of buffer */
	int len;
}WKBBufferIterator;


/*
 * A set of const pointer, length and type describing a shape in between a WKB buffer
 * This is designed to be an immutable const struct to avoid any accidental modification of the buffer
 * and also helps in avoiding memcpy from original wkb buffer to get the single geometry buffer by
 * referring to the pointers in original buffer.
 *
 * Other shape specific const properties can also be added in the future.
 */
typedef struct WKBGeometryConst
{
	/* Shape definition */
	const WKBGeometryType geometryType;
	const char *geometryStart;
	const int32 length;

	/* Polygon state */
	const char *ringPointsStart;
	const int32 numRings;
	const int32 numPoints;
} WKBGeometryConst;


typedef struct WKBVisitorFunctions
{
	/*
	 * Executed for a complete geometry represented by the WKB buffer type, it can be an atomic type such as
	 * Point, Linestring, Polygon or a Multi collection such as MultiPoint, GeometryCollection etc.
	 */
	void (*VisitGeometry)(const WKBGeometryConst *wkbGeometry, void *state);

	/*
	 * Executed for each individual geometries inside of a Multi collection geometry.
	 */
	void (*VisitSingleGeometry)(const WKBGeometryConst *wkbGeometry, void *state);


	/*
	 * Called for each individual point found during traversal, points can be part of any geometry e.g lineString, polygons rings, multipoint etc.
	 */
	void (*VisitEachPoint)(const WKBGeometryConst *wkbGeometry, void *state);

	/* Currently only called during polygon validation to check for validitiy of each ring */
	void (*VisitPolygonRing)(const WKBGeometryConst *wkbGeometry, void *state);

	/*
	 * Whether or not continue traversal of the WKB buffer, this can be used to stop the traversal
	 */
	bool (*ContinueTraversal)(void *state);
} WKBVisitorFunctions;


/* Initialize a WKBBufferIterator from given wkb buffer stringinfo */
static inline void
InitIteratorFromWKBBuffer(WKBBufferIterator *iter, StringInfo wkbBuffer)
{
	iter->headptr = wkbBuffer->data;

	/* Set currptr also to wkbBuffer->data as we start from the head */
	iter->currptr = wkbBuffer->data;

	iter->len = wkbBuffer->len;
}


/* Initialize a WKBBufferIterator from given char * and length */
static inline void
InitIteratorFromPtrAndLen(WKBBufferIterator *iter, const char *currptr, int32 len)
{
	iter->headptr = currptr;

	/* Set currptr also to wkbBuffer->data as we start from the head */
	iter->currptr = (char *) currptr;

	iter->len = len;
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
					errcode(ERRCODE_HELIO_INTERNALERROR),
					errmsg(
						"Requested to increment WKB buffer %ld bytes beyond limit.",
						overflow),
					errdetail_log(
						"Requested to increment WKB buffer %ld bytes beyond limit.",
						overflow)));
	}
}


void TraverseWKBBuffer(const StringInfo wkbBuffer, const
					   WKBVisitorFunctions *visitorFuncs, void *state);
void TraverseWKBBytea(const bytea *wkbBytea, const WKBVisitorFunctions *visitorFuncs,
					  void *state);

#endif
