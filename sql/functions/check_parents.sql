CREATE OR REPLACE FUNCTION part.check_parents() RETURNS record
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE 
    
v_count 	bigint = 0;
v_tables 	record;
v_trouble 	text[];

BEGIN

FOR v_tables IN 
    SELECT DISTINCT parent_table FROM part.part_config
LOOP

    SELECT count(1) AS n INTO v_count FROM ONLY v_tables.parent_table;
    --execute v_sql INTO v_count;

    IF v_count > 0 THEN 
        v_trouble := array_append(v_trouble, v_tables.parent_table);
        v_trouble := array_append(v_trouble, v_count);
    END IF;

	v_count := 0;

END LOOP;

RETURN v_trouble;

END
$$;
