CREATE FUNCTION @extschema@.check_control_type(p_parent_schema text, p_parent_tablename text, p_control text) RETURNS TABLE (general_type text, exact_type text) 
    LANGUAGE sql STABLE
AS $$
/* 
 * Return column type for given table & column in that table
 * Returns NULL of objects don't exist
 */

SELECT CASE 
        WHEN typname IN ('timestamptz', 'timestamp', 'date') THEN
            'time'
        WHEN typname IN ('int2', 'int4', 'int8') THEN
            'id'
       END
    , typname::text
    FROM pg_catalog.pg_type t 
    JOIN pg_catalog.pg_attribute a ON t.oid = a.atttypid
    JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = p_parent_schema::name
    AND c.relname = p_parent_tablename::name
    AND a.attname = p_control::name
$$;


