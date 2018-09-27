CREATE FUNCTION @extschema@.apply_cluster(p_parent_schema text, p_parent_tablename text, p_child_schema text, p_child_tablename text) RETURNS void
    LANGUAGE plpgsql
AS $$
DECLARE
    v_new_search_path   text := '@extschema@,pg_temp';
    v_old_search_path   text;
    v_parent_indexdef   text;
    v_relkind           char;
    v_row               record;
    v_sql               text;
BEGIN
/*
* Function to apply cluster from parent to child table
* Adapted from code fork by https://github.com/dturon/pg_partman
*/
    
SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT c.relkind INTO v_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = p_parent_schema
AND c.relname = p_parent_tablename;

IF v_relkind = 'p' THEN
    RAISE EXCEPTION 'This function cannot run on natively partitioned tables';
ELSIF v_relkind IS NULL THEN
    RAISE EXCEPTION 'Unable to find given table in system catalogs: %.%', p_parent_schema, p_parent_tablename;
END IF;

WITH parent_info AS (
    SELECT c.oid AS parent_oid 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = p_parent_schema::name
    AND c.relname = p_parent_tablename::name
)
SELECT substring(pg_get_indexdef(i.indexrelid) from ' USING .*$') AS index_def
INTO v_parent_indexdef
FROM pg_catalog.pg_index i
JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
JOIN parent_info p ON p.parent_oid = indrelid
WHERE i.indisclustered = true;

-- Loop over all existing indexes in child table to find one with matching definition
FOR v_row IN
    WITH child_info AS (
        SELECT c.oid AS child_oid 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = p_child_schema::name
        AND c.relname = p_child_tablename::name
    )
    SELECT substring(pg_get_indexdef(i.indexrelid) from ' USING .*$') AS child_indexdef
        , c.relname AS child_indexname
    FROM pg_catalog.pg_index i
    JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
    JOIN child_info p ON p.child_oid = indrelid
LOOP
    IF v_row.child_indexdef = v_parent_indexdef THEN
        v_sql = format('ALTER TABLE %I.%I CLUSTER ON %I', p_child_schema, p_child_tablename, v_row.child_indexname);
        RAISE DEBUG '%', v_sql;
        EXECUTE v_sql;
    END IF;
END LOOP;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

END;
$$;

