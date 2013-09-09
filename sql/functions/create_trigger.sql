CREATE FUNCTION create_trigger(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_function_name         text;
v_new_length            int;
v_parent_schema         text;
v_parent_tablename      text;
v_trig_name             text;
v_trig_sql              text;

BEGIN

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
-- Ensure function name matches the naming pattern
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);
v_trig_sql := 'CREATE TRIGGER '||v_trig_name||' BEFORE INSERT ON '||p_parent_table||
    ' FOR EACH ROW EXECUTE PROCEDURE '||v_function_name||'()';

EXECUTE v_trig_sql;

END
$$;
