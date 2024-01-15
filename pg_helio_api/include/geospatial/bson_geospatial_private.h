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

#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"


/*
 * MongoDB use WGS84 as the default format for storing GeoJSON data.
 * https://www.mongodb.com/docs/manual/reference/glossary/#std-term-WGS84
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
 *          errcode(MongoBadValue),
 *          errmsg("Error"),
 *          errhint("Hint")
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
 * Appends the buffer with 4 bytes, setting values to 0 at the new space and
 * returns the pointer to the starting of the 4 byte space which can be filled later
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
 *                    | => This is the returned pointer
 */
static inline uint8 *
WKBBufferAppend4EmptyBytes(StringInfo buffer)
{
	int32 num = 0;
	uint8 *current = (uint8 *) (buffer->data + buffer->len);
	appendBinaryStringInfoNT(buffer, (char *) &num, WKB_BYTE_SIZE_NUM);
	return current;
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
WriteHeaderToWKBBuffer(StringInfo buffer, WKBGeometryType type)
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
 * Write `num` (number of components to the buffer).
 * `num` can represent number of points in multipoint, number of rings in polygon, number of geometries in collection etc
 */
static inline void
WriteNumToWKBBuffer(uint8 *buffer, int32 num)
{
	memcpy(buffer, (void *) &num, WKB_BYTE_SIZE_NUM);
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
	memcpy(wkbData, wkbBuffer->data, WKB_BYTE_ORDER);

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
