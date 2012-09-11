CREATE OR REPLACE FUNCTION part.create_id_partition (p_parent_table text, p_control text, p_interval bigint, p_partition_ids bigint[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_partition_name    text;
v_tablename         text;
v_id                bigint;

BEGIN

FOREACH v_id IN ARRAY p_partition_ids LOOP
    v_partition_name := p_parent_table||'_p'||v_id;

    SELECT schemaname ||'.'|| tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;

    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF position('.' in p_parent_table) > 0 THEN 
        v_tablename := substring(v_partition_name from position('.' in v_partition_name)+1);
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS) INHERITS ('||p_parent_table||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
        CHECK ('||p_control||'>='||quote_literal(v_id)||' AND '||p_control||'<'||quote_literal(v_id + p_interval)||')';

---- Call post_script() for given parent table

END LOOP;

RETURN v_partition_name;

END
$$;
