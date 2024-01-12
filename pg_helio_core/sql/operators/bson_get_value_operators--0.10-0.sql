CREATE OPERATOR -> (
    LEFTARG = bson,
    RIGHTARG = text,
    PROCEDURE = bson_get_value
);

CREATE OPERATOR ->> (
    LEFTARG = bson,
    RIGHTARG = text,
    PROCEDURE = bson_get_value_text
);
