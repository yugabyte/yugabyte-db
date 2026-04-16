CREATE FUNCTION @extschema@.create_trigger(p_parent_table text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_function_name         text;
v_parent_schema         text;
v_parent_tablename      text;
v_relkind               char;
v_trig_name             text;
v_trig_sql              text;

BEGIN
/*
 * Function to create partitioning trigger on parent table
 */

SELECT n.nspname, c.relname, c.relkind INTO v_parent_schema, v_parent_tablename, v_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_relkind = 'p' THEN
    RAISE EXCEPTION 'This function cannot run on natively partitioned tables';
ELSIF v_relkind IS NULL THEN
    RAISE EXCEPTION 'Unable to find given table in system catalogs: %.%', v_parent_schema, v_parent_tablename;
END IF;

v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
-- Ensure function name matches the naming pattern
v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);
v_trig_sql := format('CREATE TRIGGER %I BEFORE INSERT ON %I.%I FOR EACH ROW EXECUTE PROCEDURE %I.%I()'
    , v_trig_name
    , v_parent_schema
    , v_parent_tablename
    , v_parent_schema
    , v_function_name);

EXECUTE v_trig_sql;

END
$$;

