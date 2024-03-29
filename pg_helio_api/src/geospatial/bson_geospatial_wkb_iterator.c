/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/geospatial/bson_geospatial_wkb_iterator.c
 *
 * Methods for iterating the Well Known Binary geospatial buffer
 *
 *-------------------------------------------------------------------------
 */


#include "geospatial/bson_geospatial_wkb_iterator.h"


static void TraverseWKBBufferCore(WKBBufferIterator *iter,
								  const WKBVisitorFunctions *visitorFuncs,
								  void *state, bool isCollectionGeometry);


/*
 * Traverse the WKB buffer and calls the visitor functions on each geometry / point as required.
 */
void
TraverseWKBBuffer(const StringInfo wkbBuffer, const WKBVisitorFunctions *visitorFuncs,
				  void *state)
{
	Assert(wkbBuffer != NULL && wkbBuffer->len > 0);
	WKBBufferIterator bufferIterator;
	InitIteratorFromWKBBuffer(&bufferIterator, wkbBuffer);
	bool isGeometryCollection = false;
	TraverseWKBBufferCore(&bufferIterator, visitorFuncs, state, isGeometryCollection);
}


static void
TraverseWKBBufferCore(WKBBufferIterator *iter, const WKBVisitorFunctions *visitorFuncs,
					  void *state, bool isCollectionGeometry)
{
	const char *geometryStart = iter->currptr;

	/* Read and continue the first endianess byte */
	IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_ORDER);

	/* Read GeoJSON Type */
	uint32 type = *(uint32 *) (iter->currptr);

	/* Check if type is stuffed with SRID */
	if ((type & POSTGIS_EWKB_SRID_FLAG) == POSTGIS_EWKB_SRID_FLAG)
	{
		type = type & POSTGIS_EWKB_SRID_NEGATE_FLAG;
	}
	WKBGeometryType geoType = (WKBGeometryType) type;
	IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_TYPE);

	/* Number of polygon rings if present. */
	int32 numOfRings = 0;

	switch (geoType)
	{
		case WKBGeometryType_Point:
		{
			const char *pointStart = iter->currptr;
			IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_POINT);

			if (visitorFuncs->VisitEachPoint != NULL)
			{
				const WKBGeometryConst point = {
					.geometryStart = pointStart,
					.length = iter->currptr - pointStart,
					.geometryType = WKBGeometryType_Point
				};
				visitorFuncs->VisitEachPoint(&point, state);
			}
			break;
		}

		case WKBGeometryType_LineString:
		{
			int32 numPoints = *(int32 *) (iter->currptr);
			IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_NUM);

			for (int i = 0; i < numPoints; i++)
			{
				const char *pointStart = iter->currptr;
				IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_POINT);

				if (visitorFuncs->VisitEachPoint != NULL)
				{
					const WKBGeometryConst point = {
						.geometryStart = pointStart,
						.length = iter->currptr - pointStart,
						.geometryType = WKBGeometryType_Point
					};
					visitorFuncs->VisitEachPoint(&point, state);

					if (visitorFuncs->ContinueTraversal != NULL &&
						!(visitorFuncs->ContinueTraversal(state)))
					{
						/* Stop traversing */
						return;
					}
				}
			}
			break;
		}

		case WKBGeometryType_Polygon:
		{
			numOfRings = *(int32 *) (iter->currptr);
			IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_NUM);

			for (int ring = 0; ring < numOfRings; ring++)
			{
				int32 numPoints = *(int32 *) (iter->currptr);
				IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_NUM);

				for (int point = 0; point < numPoints; point++)
				{
					const char *pointStart = iter->currptr;
					IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_POINT);

					if (visitorFuncs->VisitEachPoint != NULL)
					{
						const WKBGeometryConst point = {
							.geometryStart = pointStart,
							.length = iter->currptr - pointStart,
							.geometryType = WKBGeometryType_Point
						};
						visitorFuncs->VisitEachPoint(&point, state);

						if (visitorFuncs->ContinueTraversal != NULL &&
							!(visitorFuncs->ContinueTraversal(state)))
						{
							/* Stop traversing */
							return;
						}
					}
				}
			}
			break;
		}

		case WKBGeometryType_MultiPoint:
		case WKBGeometryType_MultiLineString:
		case WKBGeometryType_MultiPolygon:
		case WKBGeometryType_GeometryCollection:
		{
			int32 numShapes = *(int32 *) (iter->currptr);
			IncrementWKBBufferIteratorByNBytes(iter, WKB_BYTE_SIZE_NUM);

			for (int i = 0; i < numShapes; i++)
			{
				bool isCollectionGeometryNested = true;
				TraverseWKBBufferCore(iter, visitorFuncs, state,
									  isCollectionGeometryNested);
				if (visitorFuncs->ContinueTraversal != NULL &&
					!(visitorFuncs->ContinueTraversal(state)))
				{
					/* Stop traversing */
					return;
				}
			}

			break;
		}

		default:
		{
			ereport(ERROR, (
						errcode(MongoInternalError),
						errmsg("%d unexpected WKB type found during traversal.", geoType),
						errhint("%d unexpected WKB type found during traversal.",
								geoType)));
		}
	}

	if (!isCollectionGeometry && visitorFuncs->VisitGeometry != NULL)
	{
		const WKBGeometryConst geometryConst = {
			.geometryStart = geometryStart,
			.length = iter->currptr - geometryStart,
			.geometryType = geoType,
			.numberOfRings = numOfRings
		};
		visitorFuncs->VisitGeometry(&geometryConst, state);
	}

	if (isCollectionGeometry && !IsWKBCollectionType(geoType) &&
		visitorFuncs->VisitSingleGeometry != NULL)
	{
		const WKBGeometryConst geometryConst = {
			.geometryStart = geometryStart,
			.length = iter->currptr - geometryStart,
			.geometryType = geoType
		};
		visitorFuncs->VisitSingleGeometry(&geometryConst, state);
	}
}
