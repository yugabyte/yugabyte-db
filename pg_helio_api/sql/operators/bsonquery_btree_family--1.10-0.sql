
CREATE OPERATOR FAMILY __API_CATALOG_SCHEMA__.bsonquery_btree_family USING btree;

CREATE OPERATOR CLASS __API_CATALOG_SCHEMA__.bsonquery_btree_ops
    DEFAULT FOR TYPE __CORE_SCHEMA__.bsonquery USING btree FAMILY __API_CATALOG_SCHEMA__.bsonquery_btree_family AS
        OPERATOR 1 __CORE_SCHEMA__.< (__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery),
        OPERATOR 2 __CORE_SCHEMA__.<= (__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery),
        OPERATOR 3 __CORE_SCHEMA__.= (__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery),
        OPERATOR 4 __CORE_SCHEMA__.>= (__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery),
        OPERATOR 5 __CORE_SCHEMA__.> (__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery),
        FUNCTION 1 __CORE_SCHEMA__.bsonquery_compare(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery);

CREATE OPERATOR CLASS __API_CATALOG_SCHEMA__.bsonquery_bson_btree_ops
    FOR TYPE __CORE_SCHEMA__.bson USING btree FAMILY __API_CATALOG_SCHEMA__.bsonquery_btree_family AS
        OPERATOR 1 __API_CATALOG_SCHEMA__.#< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 2 __API_CATALOG_SCHEMA__.#<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 3 __API_CATALOG_SCHEMA__.#= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 4 __API_CATALOG_SCHEMA__.#>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        OPERATOR 5 __API_CATALOG_SCHEMA__.#> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery),
        FUNCTION 1 __CORE_SCHEMA__.bsonquery_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery);

CREATE OPERATOR CLASS __API_CATALOG_SCHEMA__.bson_btree_pfe_ops
    FOR TYPE __CORE_SCHEMA__.bson USING btree FAMILY __API_CATALOG_SCHEMA__.bsonquery_btree_family AS
        OPERATOR 1 __API_CATALOG_SCHEMA__.#< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 2 __API_CATALOG_SCHEMA__.#<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 3 __API_CATALOG_SCHEMA__.#= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 4 __API_CATALOG_SCHEMA__.#>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 5 __API_CATALOG_SCHEMA__.#> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 __CORE_SCHEMA__.bson_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);