DROP OPERATOR IF EXISTS __CORE_SCHEMA__.->(__CORE_SCHEMA__.bson, text);
CREATE OPERATOR __CORE_SCHEMA__.-> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = text,
    PROCEDURE = __CORE_SCHEMA__.bson_get_value
);

DROP OPERATOR IF EXISTS __CORE_SCHEMA__.->>(__CORE_SCHEMA__.bson, text);
CREATE OPERATOR __CORE_SCHEMA__.->> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = text,
    PROCEDURE = __CORE_SCHEMA__.bson_get_value_text
);
