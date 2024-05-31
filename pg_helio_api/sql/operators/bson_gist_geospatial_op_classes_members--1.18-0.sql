-- Drop the existing $geoNear operator and update the index strategy to be consistent with strategy defined in opclass/helio_gin_common.h
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_gist_geometry_ops_2d USING gist DROP OPERATOR 25 (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_gist_geography_ops_2d USING gist DROP OPERATOR 25 (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);

ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_gist_geometry_ops_2d USING gist ADD OPERATOR 30 __API_CATALOG_SCHEMA__.<|-|>(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson) FOR ORDER BY pg_catalog.float_ops;
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_gist_geography_ops_2d USING gist ADD OPERATOR 30 __API_CATALOG_SCHEMA__.<|-|>(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson) FOR ORDER BY pg_catalog.float_ops;
 
 -- Add the range operator to the index support for $geoNear
 ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_gist_geometry_ops_2d USING gist ADD OPERATOR 31 helio_api_internal.@|><|(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
 ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_gist_geography_ops_2d USING gist ADD OPERATOR 31 helio_api_internal.@|><|(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
