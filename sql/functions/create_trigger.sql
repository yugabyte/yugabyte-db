CREATE OR REPLACE FUNCTION part.create_trigger(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control               text;
v_part_interval         text;
v_table_name            text;
v_trig                  text;
v_type                  text;

BEGIN

SELECT type
    , part_interval
    , control
FROM part.part_config WHERE parent_table = p_parent_table
INTO v_type, v_part_interval, v_control;

IF position('.' in p_parent_table) > 0 THEN 
    v_table_name := substring(p_parent_table from position('.' in p_parent_table)+1);
END IF;


v_trig := 'CREATE TRIGGER '||v_table_name||'_part_trig BEFORE INSERT OR UPDATE OR DELETE ON '||p_parent_table||
    ' FOR EACH ROW EXECUTE PROCEDURE '||p_parent_table||'_part_trig_func()';

RAISE NOTICE 'v_trig: %', v_trig;
EXECUTE v_trig;

END
$$;
