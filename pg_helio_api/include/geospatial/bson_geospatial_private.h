/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_private.h
 *
 * Private data structure and function declarations for geospatial
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GEOSPATIAL_PRIVATE_H
#define BSON_GEOSPATIAL_PRIVATE_H

#include "postgres.h"
#include "utils/builtins.h"

#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "utils/mongo_errors.h"


/*
 * MongoDB use WGS84 as the default format for storing GeoJSON data.
 *
 * This is essentially the 4326 SRID (Spatial referrence ID) in PostGIS.
 * This is used to treat the geodetic data as WGS84.
 */
#define DEFAULT_GEO_SRID 4326

#define GeometryParseFlag uint32


/* Define the WKB Byte order based on the endianess */
#if __BYTE_ORDER == __LITTLE_ENDIAN
# define WKB_BYTE_ORDER (unsigned char) 1
#elif __BYTE_ORDER == __BIG_ENDIAN
# define WKB_BYTE_ORDER = (unsigned char) 0
#endif

/*
 * For a well known binary in EWKB format of postgis, the type field
 * is generally stuffed with metadata about if SRID is part of the buffer or not.
 * For more info refer postgis => liblwgeom/liblwgeom.h
 */
#define POSTGIS_EWKB_SRID_FLAG 0x20000000

/*
 * WKBBufferGetByteaWithSRID embeds SRID Flag into type when extracting bytea from WKB buffer
 * This flag is used to remove the embedding and get type from bytea
 */
#define POSTGIS_EWKB_SRID_NEGATE_FLAG 0xDFFFFFFF

/*
 * This is a useful macro defined for handling all the validity error cases for geospatial
 * and should be placed just before throwing an error to make sure we don't throw error in
 * cases where should just be ignoring them.
 *
 * This is efficient in a way where we don't allocate space for error messages even in case
 * when they are not thrown
 *
 * Usage:
 * bool shouldThrowError = true / false;
 * if (some error condition )
 * {
 *      RETURN_FALSE_IF_ERROR_NOT_EXPECTED(shouldThrowError, (
 *          errcode(ERRCODE_HELIO_BADVALUE),
 *          errmsg("Error"),
 *          errdetail_log("PII Safe error")
 *      ));
 * }
 */
#define RETURN_FALSE_IF_ERROR_NOT_EXPECTED(shouldThrow, errCodeFormat) \
	if (!shouldThrow) { return false; } \
	else \
	{ \
		ereport(ERROR, errCodeFormat); \
	}


#define EMPTY_GEO_ERROR_PREFIX ""
#define GEO_ERROR_CODE(errorCtxt) (errorCtxt ? errorCtxt->errCode : MongoBadValue)
#define GEO_ERROR_PREFIX(errorCtxt) (errorCtxt && errorCtxt->errPrefix ? \
									 errorCtxt->errPrefix(errorCtxt->document) : \
									 EMPTY_GEO_ERROR_PREFIX)
#define GEO_HINT_PREFIX(errorCtxt) (errorCtxt && errorCtxt->hintPrefix ? \
									errorCtxt->hintPrefix(errorCtxt->document) : \
									EMPTY_GEO_ERROR_PREFIX)

/* Represents byte size for the byte order in WKB */
#define WKB_BYTE_SIZE_ORDER 1

/* Represents byte size for the type of geometry in WKB */
#define WKB_BYTE_SIZE_TYPE 4

/* Represents byte size for the number of components in WKB */
#define WKB_BYTE_SIZE_NUM WKB_BYTE_SIZE_TYPE

/* Represents byte size for the number of components in WKB */
#define WKB_BYTE_SIZE_SRID WKB_BYTE_SIZE_TYPE

/* Represents byte size for the point value (includes x and y) */
#define WKB_BYTE_SIZE_POINT 16

/*
 * Radius of earth in meters according to NASA docs for spherical calculations.
 * ref - https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html
 */
static const float8 RADIUS_OF_EARTH_M = 6378.137 * 1000;

/*
 * Radius of earth in meters for spherical calculations
 * Earth is not a perfect sphere, but this is a good approximation of the radius
 * based on ellipsoid to sphere conversion model.
 * For more info: https://en.wikipedia.org/wiki/Earth_ellipsoid
 */
static const float8 RADIUS_OF_ELLIPSOIDAL_EARTH_M = 6371008.7714150595;


/*
 * GeoJSON types specified by the GeoJSON standard
 * https://datatracker.ietf.org/doc/html/rfc7946
 * and are used by Mongo.
 *
 * Please note: These are meant to be bitmask flags
 */
typedef enum GeoJsonType
{
	GeoJsonType_UNKNOWN = 0x0,
	GeoJsonType_POINT = 0x1,
	GeoJsonType_LINESTRING = 0x2,
	GeoJsonType_POLYGON = 0x4,
	GeoJsonType_MULTIPOINT = 0x8,
	GeoJsonType_MULTILINESTRING = 0x10,
	GeoJsonType_MULTIPOLYGON = 0x20,
	GeoJsonType_GEOMETRYCOLLECTION = 0x40,

	/* A custom type to select all type */
	GeoJsonType_ALL = 0xFF
} GeoJsonType;


/*
 * WKB types for commonly identified geometries
 */
typedef enum WKBGeometryType
{
	WKBGeometryType_Invalid = 0x0,
	WKBGeometryType_Point = 0x1,
	WKBGeometryType_LineString = 0x2,
	WKBGeometryType_Polygon = 0x3,
	WKBGeometryType_MultiPoint = 0x4,
	WKBGeometryType_MultiLineString = 0x5,
	WKBGeometryType_MultiPolygon = 0x6,
	WKBGeometryType_GeometryCollection = 0x7
} WKBGeometryType;


typedef const char *(*GeospatialErrorPrefixFunc)(const pgbson *);
typedef const char *(*GeospatialErrorHintPrefixFunc)(const pgbson *);

/*
 * Geospatial error context for error reporting
 */
typedef struct GeospatialErrorContext
{
	/*
	 * document referrence for error reporting, this is passed to errPrefix and hintPrefix callbacks.
	 * Callers can decide whether to ignore or extract metadata such as _id from the document to be inserted in
	 * prefix and hintPrefix
	 */
	const pgbson *document;

	/* The desired error code to be thrown */
	MongoErrorEreportCode errCode;

	/* Error prefix to be preppended for the errors, this is a callback and is only called when there is a valid error case.
	 *
	 * This helps us in not making strings available when they are not required and avoid unnecessary document traversals
	 * e.g. In geospatial case we don't want to traverse document to get the _id and send it as part of errPrefix,
	 * this might not get used overall if all valid
	 */
	GeospatialErrorPrefixFunc errPrefix;

	/* Error prefix for the hints, same as errPrefix this is also a callback.
	 * This should never include PII in the prefix returned.
	 */
	GeospatialErrorHintPrefixFunc hintPrefix;
} GeospatialErrorContext;


/*
 * Generic state while parsing any bson value to be a GeoJSON data
 */
typedef struct GeoJsonParseState
{
	/*=============== IN Variable ===================*/

	/*
	 * This helps in either throwing instant errors in case of invalidity or
	 * notifying if error was persent
	 */
	bool shouldThrowValidityError;

	/*
	 * GeoJson Expected type to parse.
	 */
	GeoJsonType expectedType;

	/*
	 * Error context to be used while throwing errors
	 */
	GeospatialErrorContext *errorCtxt;

	/*=============== OUT Variables  =================*/

	/* The type of the GeoJson found while parsing */
	GeoJsonType type;

	/* The CRS name given in the GeoJSON */
	const char *crs;

	/* Number of rings in a Polygon, for now this is used to error out $geoWithin for polygon with holes
	 * so this contains a max number of rings if geoJSON is a multipolygon.
	 */
	int32 numOfRingsInPolygon;

	/*================ INOUT Variables =================*/

	/*
	 * WKB buffer
	 */
	StringInfo buffer;
} GeoJsonParseState;


/*
 * This is basic structure that holds the x and y for all the Geomtery types
 * Postgis support
 */
typedef struct Point
{
	float8 x;
	float8 y;
} Point;


/*
 * Flags to determine how we want to parse the geometries
 */
typedef enum ParseFlags
{
	/* None */
	ParseFlag_None = 0x0,

	/* Mongo Legacy format */
	ParseFlag_Legacy = 0x2,

	/* Mongo Legacy format, while parsing it will not attempt to throw error */
	ParseFlag_Legacy_NoError = 0x4,

	/* Only GeoJSON point */
	ParseFlag_GeoJSON_Point = 0x8,

	/* Any GeoJSON type */
	ParseFlag_GeoJSON_All = 0x10,
} ParseFlags;

bool ParseBsonValueAsPoint(const bson_value_t *value,
						   bool throwError,
						   GeospatialErrorContext *errCtxt,
						   Point *outPoint);
bool ParseBsonValueAsPointWithBounds(const bson_value_t *value,
									 bool throwError,
									 GeospatialErrorContext *errCtxt,
									 Point *outPoint);

/*********Buffer Writers********/

bool BsonValueGetGeometryWKB(const bson_value_t *value,
							 const GeometryParseFlag parseFlag,
							 GeoJsonParseState *parseState);


/*
 * Get geometry from the Extended Well Known Binary (that includes the SRID)
 */
static inline Datum
GetGeometryFromWKB(const bytea *wkbBuffer)
{
	return OidFunctionCall1(PostgisGeometryFromEWKBFunctionId(),
							PointerGetDatum(wkbBuffer));
}


/*
 * Get geography from the Extended Well Known Binary (that includes the SRID)
 */
static inline Datum
GetGeographyFromWKB(const bytea *wkbBuffer)
{
	return OidFunctionCall1(PostgisGeographyFromWKBFunctionId(),
							PointerGetDatum(wkbBuffer));
}


/*
 * Returns if the GeoJSON type is a collection or Multi type
 */
static inline bool
IsWKBCollectionType(WKBGeometryType type)
{
	return type == WKBGeometryType_GeometryCollection ||
		   type == WKBGeometryType_MultiPolygon ||
		   type == WKBGeometryType_MultiPoint ||
		   type == WKBGeometryType_MultiLineString;
}


/*
 * Appends the buffer with 4 bytes, setting values to 0 at the new space and
 * returns the starting position of the 4 bytes space which can be filled later
 * with the actual value.
 *
 * This is used in cases where we don't know the multi components length at the begining
 * and since length is 4 bytes, we skip 4 bytes and fill it later.
 *
 * e.g.
 * Current buffer => 0x11110011
 * After skipping 4 bytes
 * Buffer => 0x1111001100000000
 *                    ^
 *                    | => This is the returned position of the 4byte space
 */
static inline int32
WKBBufferAppend4EmptyBytesForNums(StringInfo buffer)
{
	int32 num = 0;
	int32 currentLength = buffer->len;
	appendBinaryStringInfoNT(buffer, (char *) &num, WKB_BYTE_SIZE_NUM);
	return currentLength;
}


/*
 * This buffer writer writes the header information for a geometry
 * Header information is considered: Byte Endianess and type of geometry
 * which are the first 5 bytes, 1  byte endianess and 4 bytes type
 *
 * e.g
 * 0x01 01000000 => represents a point in little endian
 * 0x00 00000001 => represents a point in big endian
 *
 */
static inline void
WriteHeaderToWKBBuffer(StringInfo buffer, const WKBGeometryType type)
{
	char endianess = (char) WKB_BYTE_ORDER;
	appendBinaryStringInfoNT(buffer, (char *) &endianess, WKB_BYTE_SIZE_ORDER);
	appendBinaryStringInfoNT(buffer, (char *) &type, WKB_BYTE_SIZE_TYPE);
}


/*
 * Writes a simple point to the buffer
 * Point has 2 bytes double values to represent x and y coordinates
 */
static inline void
WritePointToWKBBuffer(StringInfo buffer, const Point *point)
{
	appendBinaryStringInfo(buffer, (char *) point, WKB_BYTE_SIZE_POINT);
}


/*
 * Write `num` (number of components to the buffer) at the relative position from start of buffer.
 * `num` can represent number of points in multipoint, number of rings in polygon, number of geometries in collection etc
 */
static inline void
WriteNumToWKBBufferAtPosition(StringInfo buffer, int32 relativePosition, int32 num)
{
	Assert(buffer->len > relativePosition + WKB_BYTE_SIZE_NUM);
	memcpy(buffer->data + relativePosition, (void *) &num, WKB_BYTE_SIZE_NUM);
}


/*
 * Write `num` (number of components to the buffer) to buffer.
 * `num` can represent number of points in multipoint, number of rings in polygon, number of geometries in collection etc
 */
static inline void
WriteNumToWKBBuffer(StringInfo buffer, int32 num)
{
	appendBinaryStringInfoNT(buffer, (char *) &num, WKB_BYTE_SIZE_NUM);
}


/*
 * Appends the StringInfo buffer to the WKB buffer
 */
static inline void
WriteStringInfoBufferToWKBBuffer(StringInfo wkbBuffer, StringInfo bufferToAppend)
{
	appendBinaryStringInfoNT(wkbBuffer, (char *) bufferToAppend->data,
							 bufferToAppend->len);
}


/*
 * Appends the buffer with length to the WKB buffer
 */
static inline void
WriteBufferWithLengthToWKBBuffer(StringInfo wkbBuffer, const char *bufferStart, int32
								 length)
{
	appendBinaryStringInfoNT(wkbBuffer, bufferStart, length);
}


/*
 * Appends the StringInfo buffer from the offset to the WKB buffer
 */
static inline void
WriteStringInfoBufferToWKBBufferWithOffset(StringInfo wkbBuffer, StringInfo
										   bufferToAppend, Size offset)
{
	appendBinaryStringInfoNT(wkbBuffer, (char *) bufferToAppend->data + offset,
							 bufferToAppend->len - offset);
}


/*
 * Deep frees a WKB buffer stored as StringInfo, resetStringInfo() only reset the data
 * pointer to null and doesn't clean the palloc'd memory
 */
static inline void
DeepFreeWKB(StringInfo wkbBuffer)
{
	if (wkbBuffer->data != NULL)
	{
		pfree(wkbBuffer->data);
	}
	pfree(wkbBuffer);
}


/*
 * WKBBufferGetByteaWithSRID accepts a WKB of a single geometry / geography
 * which should be of format:
 * <1B order> <4B type> <Any bytes for geometry points>.
 *
 * This function also converts the WKB to an Extended WKB with SRID stuffed in.
 * The bytea returned from this function would have the format:
 * <1B order> <4B modified type> <4B SRID> <Any bytes for geometry points>
 */
static inline bytea *
WKBBufferGetByteaWithSRID(StringInfo wkbBuffer)
{
	/* bytea will have => len (varlena header) +  */
	Size size = wkbBuffer->len + VARHDRSZ + WKB_BYTE_SIZE_SRID;
	bytea *result = (bytea *) palloc0(size);

	/* Write the size */
	SET_VARSIZE(result, size);
	uint8 *wkbData = (uint8 *) VARDATA_ANY(result);

	/* First copy the endianess */
	memcpy(wkbData, wkbBuffer->data, WKB_BYTE_SIZE_ORDER);

	/* Stuff the SRID flag in type */
	uint32 type = 0;
	memcpy(&type, (wkbBuffer->data + WKB_BYTE_SIZE_ORDER), sizeof(uint32));
	type = type | POSTGIS_EWKB_SRID_FLAG;
	memcpy((wkbData + WKB_BYTE_SIZE_ORDER), (uint8 *) &type, WKB_BYTE_SIZE_TYPE);

	/* insert SRID after type */
	uint32 srid = DEFAULT_GEO_SRID;
	uint32 sridPos = WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + sridPos), (uint8 *) &srid, WKB_BYTE_SIZE_SRID);

	/* copy data excluding endianess and type and insert it after srid */
	uint32 dataPos = sridPos + WKB_BYTE_SIZE_SRID;
	uint32 endianAndTypeLen = WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + dataPos), (wkbBuffer->data + endianAndTypeLen),
		   (wkbBuffer->len - endianAndTypeLen));

	return result;
}


/*
 * WKBBufferGetCollectionByteaWithSRID accepts a WKB of a multiple geometry / geography
 * which should be of format:
 * [<1B order> <4B type> <Any bytes for geometry points> ... ]
 *
 * This function creates a new collected type of `collectType` and stuffs in SRID and returns
 * bytea of this format:
 * <1B order> <4B collectType type> <4B SRID> <wkbBuffer (multiple values buffer)>
 */
static inline bytea *
WKBBufferGetCollectionByteaWithSRID(StringInfo wkbBuffer, WKBGeometryType collectType,
									int32 totalNum)
{
	/* bytea will have => (varlena header) + endianess + type + Srid + numofcollection + data */
	Size size = VARHDRSZ + WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE +
				WKB_BYTE_SIZE_SRID + WKB_BYTE_SIZE_NUM + wkbBuffer->len;
	bytea *result = (bytea *) palloc0(size);

	/* Write the size */
	SET_VARSIZE(result, size);
	uint8 *wkbData = (uint8 *) VARDATA_ANY(result);

	/* First write the endianess */
	memcpy(wkbData, wkbBuffer->data, WKB_BYTE_SIZE_ORDER);

	/* Stuff the SRID flag in type */
	int32 type = collectType | POSTGIS_EWKB_SRID_FLAG;
	memcpy((wkbData + WKB_BYTE_SIZE_ORDER), (uint8 *) &type, WKB_BYTE_SIZE_TYPE);

	/* insert SRID after type */
	uint32 srid = DEFAULT_GEO_SRID;
	uint32 sridPos = WKB_BYTE_SIZE_ORDER + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + sridPos), (uint8 *) &srid, WKB_BYTE_SIZE_SRID);

	/* copy number of items */
	uint32 totalPos = sridPos + WKB_BYTE_SIZE_TYPE;
	memcpy((wkbData + totalPos), (uint8 *) &totalNum, WKB_BYTE_SIZE_SRID);

	/*
	 * copy data completely, for a collection like Multipoint and GeometryCollection each
	 * each individual entries have their own endianess and type
	 */
	uint32 dataPos = totalPos + WKB_BYTE_SIZE_NUM;
	memcpy((wkbData + dataPos), wkbBuffer->data, wkbBuffer->len);

	return result;
}


#endif
