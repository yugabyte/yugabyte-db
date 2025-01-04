CREATE OR REPLACE PROCEDURE __API_SCHEMA__.drop_indexes(IN p_database_name text, IN p_arg __CORE_SCHEMA__.bson,
                                                      INOUT retval __CORE_SCHEMA__.bson DEFAULT null)
 LANGUAGE c
AS 'MODULE_PATHNAME', $procedure$command_drop_indexes$procedure$;
COMMENT ON PROCEDURE __API_SCHEMA__.drop_indexes(text, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
    IS 'drop index(es) from a Mongo collection';