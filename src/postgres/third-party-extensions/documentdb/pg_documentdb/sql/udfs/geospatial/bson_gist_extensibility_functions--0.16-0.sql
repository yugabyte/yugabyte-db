-- Index options function for bson_gist_geometry_ops_2d operator class family
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geometry_2d_options(internal)
 RETURNS void
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_gist_geometry_2d_options$function$;

-- GIST compress support function for 2d index, this internally uses Postgis GIST compress support function
-- geometry_gist_compress_2d.
-- It extract the geomtery from a `bson` document and compresses it into the box2df type for storage. It also
-- apply additional validations on geometries e.g. bound checks.
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geometry_2d_compress(internal)
 RETURNS INTERNAL
 LANGUAGE C
 PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_gist_geometry_2d_compress$function$;

-- GIST distance support functions for bson, used for ORDER BY and nearest neighbour queries
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geometry_distance_2d(internal, __CORE_SCHEMA__.bson, integer)
 RETURNS float8
 LANGUAGE C
 PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_gist_geometry_distance_2d$function$;

-- GIST consistent support functions for bson, which checks if the query can be satisfied by the key stored in index
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geometry_consistent_2d(internal, __CORE_SCHEMA__.bson, integer)
 RETURNS bool
 LANGUAGE C
 PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_gist_geometry_consistent_2d$function$;

-- Geography type GIST support functions for bson

-- Index options function for bson_gist_geography_ops_2d operator class family
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geography_options(internal)
 RETURNS void
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_gist_geography_options$function$;

-- GIST compress support function for 2dsphere index, this internally uses Postgis GIST compress support function
-- It extract the geography from a `bson` document and compresses it into the gidx type for storage
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geography_compress(internal)
 RETURNS INTERNAL
 LANGUAGE C
 PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_gist_geography_compress$function$;

-- GIST distance support functions for bson, used for ORDER BY and nearest neighbour queries
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geography_distance(internal, __CORE_SCHEMA__.bson, integer)
 RETURNS float8
 LANGUAGE C
 PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_gist_geography_distance$function$;

-- GIST consistent support functions for bson, which checks if the query can be satisfied by the key stored in index
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_gist_geography_consistent(internal, __CORE_SCHEMA__.bson, integer)
 RETURNS bool
 LANGUAGE C
 PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_gist_geography_consistent$function$;

-- bson_validate_geography function is used to make the 2dsphere indexes sparse with IS NOT NULL construct
-- and also used with geospatial query operators to match the 2dsphere index predicate
-- for detailed info look at the description of `bson_validate_geography` C function
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_validate_geography(
    p_document __CORE_SCHEMA__.bson,
    p_keyPath text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_validate_geography$function$;

-- for detailed info look at the description of `bson_validate_geometry` C function
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_validate_geometry(
    p_document __CORE_SCHEMA__.bson,
    p_keyPath text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_validate_geometry$function$;
