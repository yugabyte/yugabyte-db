
-- Creating a hash operator class for index type hash is used by the postgres planner
-- if there's a GROUP BY, DISTINCT, or grouping operations in general. If a hashable
-- type is available, then a HashAgg is created instead of sorting/grouping which 
-- could be more efficient and consume less memory and take less time.
CREATE OPERATOR CLASS __CORE_SCHEMA__.bson_hash_ops
    DEFAULT FOR TYPE __CORE_SCHEMA__.bson USING hash AS
        OPERATOR 1 __CORE_SCHEMA__.= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 __CORE_SCHEMA__.bson_hash_int4(__CORE_SCHEMA__.bson),
        FUNCTION 2 __CORE_SCHEMA__.bson_hash_int8(__CORE_SCHEMA__.bson, int8);