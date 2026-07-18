CREATE FUNCTION @extschema@.drop_partition_column(p_parent_table text, p_column text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_parent_oid        oid;
v_parent_schema     text;
v_parent_tablename  text;
v_row               record;

BEGIN
/*
 * Function to ensure a column is dropped in all child tables, no matter when it was created
 */
SELECT c.oid, n.nspname, c.relname INTO v_parent_oid, v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_parent_oid IS NULL THEN
    RAISE EXCEPTION 'Given parent table does not exist: %', p_parent_table;
END IF;

EXECUTE format('ALTER TABLE %I.%I DROP COLUMN IF EXISTS %I', v_parent_schema, v_parent_tablename, p_column);

FOR v_row IN 
    SELECT n.nspname AS child_schema, c.relname AS child_table
    FROM pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON h.inhrelid = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE inhparent = v_parent_oid 
LOOP

    EXECUTE format('ALTER TABLE %I.%I DROP COLUMN IF EXISTS %I', v_row.child_schema, v_row.child_table, p_column);

END LOOP;

END
$$;


