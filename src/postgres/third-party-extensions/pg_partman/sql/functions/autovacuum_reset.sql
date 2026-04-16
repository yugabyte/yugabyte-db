CREATE FUNCTION @extschema@.autovacuum_reset(p_parent_schema text, p_parent_tablename text, p_source_schema text DEFAULT NULL, p_source_tablename text DEFAULT NULL) RETURNS boolean
    LANGUAGE plpgsql
    AS $$
DECLARE

v_row       record;
v_sql       text;

BEGIN

    v_sql = format('ALTER TABLE %I.%I RESET (autovacuum_enabled, toast.autovacuum_enabled)', p_parent_schema, p_parent_tablename);
    RAISE DEBUG 'partition_data sql: %', v_sql;
    EXECUTE v_sql;

    IF p_source_tablename IS NOT NULL THEN
        v_sql = format('ALTER TABLE %I.%I RESET (autovacuum_enabled, toast.autovacuum_enabled)', p_source_schema, p_source_tablename);
        RAISE DEBUG 'partition_data sql: %', v_sql;
        EXECUTE v_sql;
    END IF;

    FOR v_row IN 
        SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_schema||'.'||p_parent_tablename, 'ASC')
    LOOP
        v_sql = format('ALTER TABLE %I.%I RESET (autovacuum_enabled, toast.autovacuum_enabled)', v_row.partition_schemaname, v_row.partition_tablename);
        RAISE DEBUG 'partition_data sql: %', v_sql;
        EXECUTE v_sql;
    END LOOP;

    RETURN true;
END
$$;

