
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_single_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 26 helio_api_internal.@!> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_single_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 27 helio_api_internal.@!>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_single_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 28 helio_api_internal.@!< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_single_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 29 helio_api_internal.@!<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);

ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_wildcard_project_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 26 helio_api_internal.@!> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_wildcard_project_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 27 helio_api_internal.@!>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_wildcard_project_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 28 helio_api_internal.@!< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
ALTER OPERATOR FAMILY __API_CATALOG_SCHEMA__.bson_rum_wildcard_project_path_ops USING __EXTENSION_OBJECT__(_rum) ADD OPERATOR 29 helio_api_internal.@!<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);
