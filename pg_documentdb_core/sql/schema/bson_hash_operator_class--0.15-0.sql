
-- Creating a hash operator class for index type hash is used by the postgres planner
-- if there's a GROUP BY, DISTINCT, or grouping operations in general. If a hashable
-- type is available, then a HashAgg is created instead of sorting/grouping which 
-- could be more efficient and consume less memory and take less time.
CREATE OPERATOR CLASS bson_hash_ops
    DEFAULT FOR TYPE bson USING hash AS
        OPERATOR 1 = (bson, bson),
        FUNCTION 1 bson_hash_int4(bson),
        FUNCTION 2 bson_hash_int8(bson, int8);