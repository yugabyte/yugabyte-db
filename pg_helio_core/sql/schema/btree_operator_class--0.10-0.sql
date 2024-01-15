CREATE OPERATOR CLASS bson_btree_ops
    DEFAULT FOR TYPE bson USING btree AS
        OPERATOR 1 < (bson, bson),
        OPERATOR 2 <= (bson, bson),
        OPERATOR 3 = (bson, bson),
        OPERATOR 4 >= (bson, bson),
        OPERATOR 5 > (bson, bson),
        FUNCTION 1 bson_compare(bson, bson);