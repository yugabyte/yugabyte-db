CREATE PROCEDURE @extschema@.undo_partition_proc(
    p_parent_table text
    , p_interval text DEFAULT NULL
    , p_batch int DEFAULT NULL
    , p_wait int DEFAULT 1
    , p_target_table text DEFAULT NULL
    , p_keep_table boolean DEFAULT true
    , p_lock_wait int DEFAULT 0
    , p_lock_wait_tries int DEFAULT 10
    , p_quiet boolean DEFAULT false
    , p_ignored_columns text[] DEFAULT NULL
    , p_drop_cascade boolean DEFAULT false)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock                  boolean;
v_batch_count               int := 0;
v_is_autovac_off            boolean := false;
v_lockwait_count            int := 0;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_type            text;
v_partitions_undone         int;
v_partitions_undone_total   int := 0;
v_row                       record;
v_rows_undone               bigint;
v_target_schema             text;
v_target_tablename          text;
v_sql                       text;
v_total                     bigint := 0;

BEGIN

/* YB: advisory lock not supported
v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition_proc'), hashtext(p_parent_table));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman undo_partition_proc already running for given parent table: %.', p_parent_table;
    RETURN;
END IF;
*/

SELECT partition_type
INTO v_partition_type
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for given table: %', p_parent_table;
END IF;

IF v_partition_type = 'native' AND p_target_table IS NULL THEN
    RAISE EXCEPTION 'Natively partitioned table sets require setting the p_target_table parameter to undo partitioning.';
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Ensure it is schema qualified: %', p_parent_table;
    END IF;

IF p_target_table IS NOT NULL THEN
    SELECT n.nspname, c.relname INTO v_target_schema, v_target_tablename
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(p_target_table, '.', 1)::name
    AND c.relname = split_part(p_target_table, '.', 2)::name;
        IF v_target_tablename IS NULL THEN
            RAISE EXCEPTION 'Unable to find given target table in system catalogs. Ensure it is schema qualified: %', p_target_table;
        END IF;
END IF;

/*
-- Currently no way to catch exception and reset autovac settings back to normal. Until I can do that, leaving this feature out for now
-- Leaving the functions to turn off/reset in to let people do that manually if desired
IF p_autovacuum_on = false THEN         -- Add this parameter back to definition when this is working
    -- Turn off autovac for parent, source table if set, and all child tables
    v_is_autovac_off := @extschema@.autovacuum_off(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
    COMMIT;
END IF;
*/

v_sql := format('SELECT partitions_undone, rows_undone FROM %I.undo_partition (%L, p_keep_table := %L, p_lock_wait := %L'
        , '@extschema@'
        , p_parent_table
        , p_keep_table
        , p_lock_wait);
IF p_interval IS NOT NULL THEN
    v_sql := v_sql || format(', p_batch_interval := %L', p_interval);
END IF;
IF p_target_table IS NOT NULL THEN
    v_sql := v_sql || format(', p_target_table := %L', p_target_table);
END IF;
IF p_ignored_columns IS NOT NULL THEN
    v_sql := v_sql || format(', p_ignored_columns := %L', p_ignored_columns);
END IF;
IF p_drop_cascade IS NOT NULL THEN
    v_sql := v_sql || format(', p_drop_cascade := %L', p_drop_cascade);
END IF;
v_sql := v_sql || ')';
RAISE DEBUG 'partition_data sql: %', v_sql;

LOOP
    EXECUTE v_sql INTO v_partitions_undone, v_rows_undone;
    -- If lock wait timeout, do not increment the counter
    IF v_rows_undone != -1 THEN
        v_batch_count := v_batch_count + 1;
        v_partitions_undone_total := v_partitions_undone_total + v_partitions_undone;
        v_total := v_total + v_rows_undone;
        v_lockwait_count := 0;
    ELSE
        v_lockwait_count := v_lockwait_count + 1;
        IF v_lockwait_count > p_lock_wait_tries THEN
            RAISE EXCEPTION 'Quitting due to inability to get lock on next batch of rows to be moved';
        END IF;
    END IF;
    IF p_quiet = false THEN
        IF v_rows_undone > 0 THEN
            RAISE NOTICE 'Batch: %, Partitions undone this batch: %, Rows undone this batch: %', v_batch_count, v_partitions_undone, v_rows_undone;
        ELSIF v_rows_undone = -1 THEN
            RAISE NOTICE 'Unable to obtain row locks for data to be moved. Trying again...';
        END IF;
    END IF;
    COMMIT;

    -- If no rows left or given batch argument limit is reached
    IF v_rows_undone = 0 OR (p_batch > 0 AND v_batch_count >= p_batch) THEN
        EXIT;
    END IF;

    -- undo_partition functions will remove config entry once last child is dropped
    -- Added here to handle edge-case
    SELECT partition_type
    INTO v_partition_type
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table;
    IF NOT FOUND THEN
        EXIT;
    END IF;

    PERFORM pg_sleep(p_wait);
    
    RAISE DEBUG 'v_partitions_undone: %, v_rows_undone: %, v_batch_count: %, v_total: %, v_lockwait_count: %, p_wait: %', v_partitions_undone, p_wait, v_rows_undone, v_batch_count, v_total, v_lockwait_count;
END LOOP;

/*
IF v_is_autovac_off = true THEN
    -- Reset autovac back to default if it was turned off by this procedure
    PERFORM @extschema@.autovacuum_reset(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
    COMMIT; 
END IF;
*/

IF p_quiet = false THEN
    RAISE NOTICE 'Total partitions undone: %, Total rows moved: %', v_partitions_undone_total, v_total;
END IF;
RAISE NOTICE 'Ensure to VACUUM ANALYZE the old parent & target table after undo has finished';

END
$$;

