
CREATE OPERATOR FAMILY __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_pfe_btree_family USING btree;

-- This is the comparison clause for predicate proof.
CREATE OPERATOR CLASS __API_SCHEMA_INTERNAL_V2__.bsonquery_indexbounds_btree_pfe_compare_ops
    FOR TYPE __CORE_SCHEMA__.bsonquery USING btree FAMILY __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_pfe_btree_family AS
        OPERATOR 1 __API_SCHEMA_INTERNAL_V2__.< (__CORE_SCHEMA__.bsonquery, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 2 __API_SCHEMA_INTERNAL_V2__.<= (__CORE_SCHEMA__.bsonquery, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 3 __API_SCHEMA_INTERNAL_V2__.= (__CORE_SCHEMA__.bsonquery, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 4 __API_SCHEMA_INTERNAL_V2__.>= (__CORE_SCHEMA__.bsonquery, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 5 __API_SCHEMA_INTERNAL_V2__.> (__CORE_SCHEMA__.bsonquery, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds);

-- This is what's on the index PFE
CREATE OPERATOR CLASS __API_SCHEMA_INTERNAL_V2__.bson_btree_pfe_base_ops
    FOR TYPE __CORE_SCHEMA__.bson USING btree FAMILY __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_pfe_btree_family AS
        OPERATOR 1 __API_CATALOG_SCHEMA__.#< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 2 __API_CATALOG_SCHEMA__.#<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 3 __API_CATALOG_SCHEMA__.#= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 4 __API_CATALOG_SCHEMA__.#>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 5 __API_CATALOG_SCHEMA__.#> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery);

-- This is the query clause that's created
CREATE OPERATOR CLASS __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_btree_pfe_ops
    FOR TYPE __CORE_SCHEMA__.bson USING btree FAMILY __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_pfe_btree_family AS
        OPERATOR 1 __API_SCHEMA_INTERNAL_V2__.##< (__CORE_SCHEMA__.bson, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 2 __API_SCHEMA_INTERNAL_V2__.##<= (__CORE_SCHEMA__.bson, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 3 __API_SCHEMA_INTERNAL_V2__.##= (__CORE_SCHEMA__.bson, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 4 __API_SCHEMA_INTERNAL_V2__.##>= (__CORE_SCHEMA__.bson, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds),
        OPERATOR 5 __API_SCHEMA_INTERNAL_V2__.##> (__CORE_SCHEMA__.bson, __API_SCHEMA_INTERNAL_V2__.bsonindexbounds);