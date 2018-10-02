-- Initial release to provide support PostgreSQL 11. All PG11 functionality is subject to change before its final stable release.

-- IMPORTANT POSTGRESQL 11 UPGRADE STEPS
    -- All foreign keys, any non-unique indexes, and any unique indexes that include the partition key must be created on the actual parent table and removed from the template table for those property inheritances to continue working. Foreign key inheritance from the template table no longer works at all.
    -- Any unique indexes or primary keys that do not include the partition column must still use the template table in order to be inherited to child tables.
    -- Indexes defined on the parent will take precidence over those defined on the template table. Duplicate indexes should not happen. If they do, please report this as a bug. 

-- Note that if this version is installed on PG10 and older, the procedures will not be created. A migration script to update pg_partman to include those will be made available once PG11 stable has been released.

-- Major change in privilege/security of this extension. SECURITY DEFINER is no longer in use to cause most of pg_partman's functions to run as the owner of the called function. This is mostly due to the requirements to support PROCEDUREs in PG11, but it's also generally a better idea for security in the long run.
    -- It is recommended to create a dedicated role for pg_partman maintenance runs and give it the privileges outlined in the setup section of the top level README.
    -- The role running maintenance must now have permissions to create tables in the target schema. If you start seeing permission errors after upgrading, this is most likely what needs to be fixed.
    -- The role running maintenance must now always be the owner of the tables it is managing. This shouldn't be too drastic of a change since previously it was making the maintenace function owner the owner of the partitioned tables. However, if you've changed partition table ownership after initial creation, you may run into issues until you set the pg_partman role as the owner.
    -- If you are using logical replication, the partman maintenance role must be the owner of the publication in use.
    -- As a bonus, the ALTER DEFAULT PRIVILEGES feature should now be much easier to use since you have a dedicated role for maintaining partitions. Check the documentation for this feature so that you can set any read/write privileges you want new partition sets to have at the time they are initially created.
    -- Two functions retain their SECURITY DEFINER status: 
        -- check_name_length() - Used inside the trigger function and I don't want to break existing installations. It's an IMMUTABLE function so the security implications aren't that great.
        -- apply_privileges() - Requires access to pg_authid & also potentially changes object ownership.
    -- If you're using pg_jobmon, privileges for that extension are managed independently of pg_partman. See that extension's documentation for privileges it requires.    

-- For PostgreSQL 11+, started conversion of some python scripts into PROCEDUREs that are now installed within the database. Scripts are still available for pre-11 versions, but will be phased out as soon as PG10 is no longer under support.
    -- Note that the user that calls these procedures must have permissions to write to the partition set and create/drop partitions in the set. 
    -- partition_data.py script is now the partition_data_proc() PROCEDURE.
    -- undo_partition.py script is now the undo_partition_proc() PROCEDURE. 
    -- reapply_constraints.py is now the reapply_constraints_proc() PROCEDURE.
    -- reapply_foreign_keys.py not converted since it's not needed for PG11+. Foreign keys can be applied to the parent and will automatically be inherited.
    -- reapply_indexes.py was not converted yet because concurrent index creation is not yet possible inside a PROCEDURE.
    -- Unable to turn autovacuum off/on reliably in PROCEDURE as was previously done with python scripts. If autovac is an issue when (un)partitioning large tables, recommend manually disabling it.

-- Consolidated undo_partition_time(), undo_partition_id(), and undo_partition_native() functions into a single undo_partition() function.
-- The old undo_partition() function that just copied data from child tables to the parent has been removed. The only utility this really had was a possible way to support undoing partition sets not maintained by pg_partman. With native partitioning now in place, its continued usability is now questionable and it was also causing confusion since people were using it and not the previous ones specialized for pg_partman. Can incorporate back in if someone can provide a further beneficial use case.

-- For PG11+, a DEFAULT partition is automatically created as part of the partition set to handle any data that does not have a matching child table. 
    -- It's just given a _default suffix on the existing parent table name.
    -- Support within pg_partman for moving data out of the default partition is not included in 4.0.0, but will be included in a future release.
    -- New parameter to show_partitions() can include the DEFAULT in its output, but it's not output unless specifically requested since it's not commonly needed in internal code where it's mostly used.

-- For native partitioning, added option to give a "source" table to the partition_data*() functions that is different from the parent table of the partition set. This assists with partitioning an existing table to a natively partitioned table.

-- For PostgreSQL 11+, run_maintenance_proc() has been added and should cause much less contention when running against many large partition sets in a single call.
    -- For PG11+, this is the preferred maintenance method to run over the old run_maintenance() function. The old one will continue to work for the next few major releases, but please update all pg_partman maintenence calls in your environment to use the new procedure.
    
-- Simplified --type argument for undo_partition.py script for PG versions 10 and below. Similar to create_parent(), values are now 'partman' or 'native'.

-- Maintenance that creates new partitions on PG11+ and is using native partitioning will no longer analyze the partition sets by default. This should greatly cut down on maintenance run time and contention issues. If you're noticing any odd query plans, especially if you're using additionally configured constraints, you may need to schedule some manual analyzes or you can set the p_analyze parameter in either the function or procedure call to "true".

-- Fixed bug in run_maintenance() with subpartitioning where it would throw an error if retention dropped one of the parent tables undergoing maintenance.

-- Fixed template table inheritance for PG10 not working on minor version PostgreSQL 10.0 (but you really shouldn't be running this version anymore).

-- Fixed compilation errors for background worker against PostgreSQL 11.


-- ########################  START POSTGRESQL 11 ONLY SECTION ##############################
DO $pg11only$
DECLARE

v_partition_data_sql        text;
v_reapply_constraints_sql   text;
v_reapply_foreign_keys_sql  text;
v_run_maintenance_sql       text;
v_undo_partition_sql        text;

BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
-- ########################  START POSTGRESQL 11 ONLY SECTION ##############################


-- Syntax check during extension updating doesn't even allow the CREATE PROCEDURE statement to exist. Using dollar quoting w/ EXECUTE to get around it in this conditional IF block.
v_partition_data_sql := $partition_data$
CREATE PROCEDURE @extschema@.partition_data_proc (p_parent_table text, p_interval text DEFAULT NULL, p_batch int DEFAULT NULL, p_wait int DEFAULT 1, p_source_table text DEFAULT NULL, p_order text DEFAULT 'ASC', p_lock_wait int DEFAULT 0, p_lock_wait_tries int DEFAULT 10, p_quiet boolean DEFAULT false)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock          boolean;
v_batch_count       int := 0;
v_control           text;
v_control_type      text;
v_is_autovac_off    boolean := false;
v_lockwait_count    int := 0;
v_parent_schema     text;
v_parent_tablename  text;
v_row               record;
v_rows_moved        bigint;
v_source_schema     text;
v_source_tablename  text;
v_sql               text;
v_total             bigint := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman partition_data_proc'), hashtext(p_parent_table));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman partition_data_proc already running for given parent table: %.', p_parent_table;
    RETURN;
END IF;

SELECT control
INTO v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for given table: %', p_parent_table;
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Ensure it is schema qualified: %', p_parent_table;
    END IF;

IF p_source_table IS NOT NULL THEN
    SELECT n.nspname, c.relname INTO v_source_schema, v_source_tablename
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(p_source_table, '.', 1)::name
    AND c.relname = split_part(p_source_table, '.', 2)::name;
        IF v_source_tablename IS NULL THEN
            RAISE EXCEPTION 'Unable to find given source table in system catalogs. Ensure it is schema qualified: %', p_source_table;
        END IF;
END IF;
 
SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

/*
-- Currently no way to catch exception and reset autovac settings back to normal. Until I can do that, leaving this feature out for now
-- Leaving the functions to turn off/reset in to let people do that manually if desired
IF p_autovacuum_on = false THEN         -- Add this parameter back to definition when this is working
    -- Turn off autovac for parent, source table if set, and all child tables
    v_is_autovac_off := @extschema@.autovacuum_off(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
    COMMIT;
END IF;
*/

v_sql := format('SELECT %I.partition_data_%s (%L, p_lock_wait := %L, p_order := %L, p_analyze := false'
        , '@extschema@', v_control_type, p_parent_table, p_lock_wait, p_order);
IF p_interval IS NOT NULL THEN
    v_sql := v_sql || format(', p_batch_interval := %L', p_interval);
END IF;
IF p_source_table IS NOT NULL THEN
    v_sql := v_sql || format(', p_source_table := %L', p_source_table);
END IF;
v_sql := v_sql || ')';
RAISE DEBUG 'partition_data sql: %', v_sql;

LOOP
    EXECUTE v_sql INTO v_rows_moved;
    -- If lock wait timeout, do not increment the counter
    IF v_rows_moved != -1 THEN
        v_batch_count := v_batch_count + 1;
        v_total := v_total + v_rows_moved;
        v_lockwait_count := 0;
    ELSE
        v_lockwait_count := v_lockwait_count + 1;
        IF v_lockwait_count > p_lock_wait_tries THEN
            RAISE EXCEPTION 'Quitting due to inability to get lock on next batch of rows to be moved';
        END IF;
    END IF;
    IF p_quiet = false THEN
        IF v_rows_moved > 0 THEN
            RAISE NOTICE 'Batch: %, Rows moved: %', v_batch_count, v_rows_moved;
        ELSIF v_rows_moved = -1 THEN
            RAISE NOTICE 'Unable to obtain row locks for data to be moved. Trying again...';
        END IF;
    END IF;
    -- If no rows left or given batch argument limit is reached
    IF v_rows_moved = 0 OR (p_batch > 0 AND v_batch_count >= p_batch) THEN
        EXIT;
    END IF;
    COMMIT;
    PERFORM pg_sleep(p_wait);
    RAISE DEBUG 'v_rows_moved: %, v_batch_count: %, v_total: %, v_lockwait_count: %, p_wait: %', p_wait, v_rows_moved, v_batch_count, v_total, v_lockwait_count;
END LOOP;

/*
IF v_is_autovac_off = true THEN
    -- Reset autovac back to default if it was turned off by this procedure
    PERFORM @extschema@.autovacuum_reset(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
    COMMIT; 
END IF;
*/

IF p_quiet = false THEN
    RAISE NOTICE 'Total rows moved: %', v_total;
END IF;
RAISE NOTICE 'Ensure to VACUUM ANALYZE the parent (and source table if used) after partitioning data';

/* Leaving here until I can figure out what's wrong with procedures and exception handling
EXCEPTION
    WHEN QUERY_CANCELED THEN
        ROLLBACK;
        -- Reset autovac back to default if it was turned off by this procedure
        IF v_is_autovac_off = true THEN
            PERFORM @extschema@.autovacuum_reset(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        ROLLBACK;
        -- Reset autovac back to default if it was turned off by this procedure
        IF v_is_autovac_off = true THEN
            PERFORM @extschema@.autovacuum_reset(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
*/
END;
$$;
$partition_data$;

EXECUTE v_partition_data_sql;


v_undo_partition_sql := $undo_partition$
CREATE PROCEDURE @extschema@.undo_partition_proc(p_parent_table text, p_interval text DEFAULT NULL, p_batch int DEFAULT NULL, p_wait int DEFAULT 1, p_target_table text DEFAULT NULL, p_keep_table boolean DEFAULT true, p_lock_wait int DEFAULT 0, p_lock_wait_tries int DEFAULT 10, p_quiet boolean DEFAULT false) 
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

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition_proc'), hashtext(p_parent_table));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman partition_data_proc already running for given parent table: %.', p_parent_table;
    RETURN;
END IF;

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
            RAISE EXCEPTION 'Unable to find given target table in system catalogs. Ensure it is schema qualified: %', p_source_table;
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
        , '@extschema@', p_parent_table, p_keep_table, p_lock_wait);
IF p_interval IS NOT NULL THEN
    v_sql := v_sql || format(', p_batch_interval := %L', p_interval);
END IF;
IF p_target_table IS NOT NULL THEN
    v_sql := v_sql || format(', p_target_table := %L', p_target_table);
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
$undo_partition$;

EXECUTE v_undo_partition_sql;


v_run_maintenance_sql := $run_maintenance$
CREATE PROCEDURE @extschema@.run_maintenance_proc(p_wait int DEFAULT 0, p_analyze boolean DEFAULT NULL, p_jobmon boolean DEFAULT true, p_debug boolean DEFAULT false)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock              boolean;
v_row                   record;
v_sql                   text;
v_tables_list_sql       text;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = false THEN
    RAISE NOTICE 'Partman maintenance already running or another session has not released its advisory lock.';
    RETURN;
END IF;

v_tables_list_sql := 'SELECT parent_table
            FROM @extschema@.part_config
            WHERE undo_in_progress = false
            AND automatic_maintenance = ''on''';
 
FOR v_row IN EXECUTE v_tables_list_sql
LOOP
/*
 * Run maintenance with a commit between each partition set
 * TODO - Once PG11 is more mainstream, see about more full conversion of run_maintenance function as well as turning 
 *        create_partition* functions into procedures to commit after every child table is made. May need to wait
 *        for more PROCEDURE features as well (return values, search_path, etc).
 *      - Also see about swapping names so this is the main object to call for maintenance instead of a function.
 */
    v_sql := format('SELECT %I.run_maintenance(%L, p_jobmon := %L, p_debug := %L',
        '@extschema@', v_row.parent_table, p_jobmon, p_debug);

    IF p_analyze IS NOT NULL THEN
        v_sql := v_sql || format(', p_analyze := %L', p_analyze);
    END IF;
        
    v_sql := v_sql || ')';

    IF p_debug THEN
        RAISE NOTICE 'v_sql run_maintenance_proc: %', v_sql;
    END IF;

    EXECUTE v_sql;
    COMMIT;

    PERFORM pg_sleep(p_wait);

END LOOP;

PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
END
$$;
$run_maintenance$;
EXECUTE v_run_maintenance_sql;


v_reapply_constraints_sql := $reapply_constraints$
CREATE PROCEDURE @extschema@.reapply_constraints_proc(p_parent_table text, p_drop_constraints boolean DEFAULT false, p_apply_constraints boolean DEFAULT false, p_wait int DEFAULT 0, p_dryrun boolean DEFAULT false)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock                      boolean;
v_child_stop                    text;
v_control                       text;
v_control_type                  text;
v_datetime_string               text;
v_epoch                         text;
v_last_partition                text;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamptz;
v_optimize_constraint           int;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_interval            text;
v_partition_suffix              text;
v_premake                       int;
v_row                           record;
v_sql                           text;

BEGIN
/*
 * Procedure for reapplying additional constraints managed by pg_partman on child tables. See docs for additional info on this special constraint management. 
 * Procedure can run in two distinct modes: 1) Drop all constraints  2) Apply all constraints. 
 * If both modes are run in a single call, drop is run before apply.
 * Typical usage would be to run the drop mode, edit the data, then run apply mode to re-create all constraints on a partition set."
 */

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman reapply_constraints'));
IF v_adv_lock = false THEN
    RAISE NOTICE 'Partman reapply_constraints_proc already running or another session has not released its advisory lock.';
    RETURN;
END IF;


SELECT control, premake, optimize_constraint, datetime_string, epoch, partition_interval
INTO v_control, v_premake, v_optimize_constraint, v_datetime_string, v_epoch, v_partition_interval
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF v_premake IS NULL THEN
    RAISE EXCEPTION 'Unable to find given parent in pg_partman config: %. This procedure is only meant to be called on pg_partman managed partition sets.', p_parent_table;
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Ensure it is schema qualified: %', p_parent_table;
    END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

-- Determine child table to stop creating constraints on based on optimize_constraint value
-- Same code in apply_constraints.sql
SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
    SELECT child_start_time INTO v_last_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, p_parent_table);
    v_partition_suffix := to_char(v_last_partition_timestamp - (v_partition_interval::interval * (v_optimize_constraint + v_premake + 1) ), v_datetime_string);
ELSIF v_control_type = 'id' THEN
    SELECT child_start_id INTO v_last_partition_id FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, p_parent_table);
    v_partition_suffix := (v_last_partition_id - (v_partition_interval::int * (v_optimize_constraint + v_premake + 1) ))::text; 
END IF;

v_child_stop := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

v_sql := format('SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(%L, %L)', p_parent_table, 'ASC');

RAISE DEBUG 'reapply_constraint: v_parent_tablename: % , v_partition_suffix: %, v_child_stop: %,  v_sql: %', v_parent_tablename, v_partition_suffix, v_child_stop, v_sql;

v_row := NULL;
FOR v_row IN EXECUTE v_sql LOOP
    IF p_drop_constraints THEN
        IF p_dryrun THEN
            RAISE NOTICE 'DRYRUN NOTICE: Dropping constraints on child table: %.%', v_row.partition_schemaname, v_row.partition_tablename;
        ELSE
            RAISE DEBUG 'reapply_constraint drop: %.%', v_row.partition_schemaname, v_row.partition_tablename;
            PERFORM @extschema@.drop_constraints(p_parent_table, format('%s.%s', v_row.partition_schemaname, v_row.partition_tablename)::text);
        END IF;
    END IF; -- end drop
    COMMIT;

    IF p_apply_constraints THEN
        IF p_dryrun THEN
            RAISE NOTICE 'DRYRUN NOTICE: Applying constraints on child table: %.%', v_row.partition_schemaname, v_row.partition_tablename;
        ELSE
            RAISE DEBUG 'reapply_constraint apply: %.%', v_row.partition_schemaname, v_row.partition_tablename;
            PERFORM @extschema@.apply_constraints(p_parent_table, format('%s.%s', v_row.partition_schemaname, v_row.partition_tablename)::text);
        END IF;
    END IF; -- end apply

    IF v_row.partition_tablename = v_child_stop THEN
        RAISE DEBUG 'reapply_constraint: Reached stop at %.%', v_row.partition_schemaname, v_row.partition_tablename; 
        EXIT; -- stop creating constraints after optimize target is reached
    END IF;
    COMMIT;
    PERFORM pg_sleep(p_wait);
END LOOP;

EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

PERFORM pg_advisory_unlock(hashtext('pg_partman reapply_constraints'));
END
$$;
$reapply_constraints$;
EXECUTE v_reapply_constraints_sql;



-- ########################  END POSTGRESQL 11 ONLY SECTION ##############################
END IF;
END
$pg11only$;
-- ########################  END POSTGRESQL 11 ONLY SECTION ##############################



CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.partition_data_id(text, int, bigint, numeric, text, boolean, text) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'partition_data_time'; 


INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.partition_data_time(text, int, interval, numeric, text, boolean, text) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'partition_data_time'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.undo_partition(text, int, text, boolean, numeric, text) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'undo_partition_native'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.show_partitions(text, text, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = ''; 

DROP FUNCTION @extschema@.partition_data_id(text, int, bigint, numeric, text, boolean);
DROP FUNCTION @extschema@.partition_data_time(text, int, interval, numeric, text, boolean); 
DROP FUNCTION @extschema@.undo_partition_time(text, int, interval, boolean, numeric);
DROP FUNCTION @extschema@.undo_partition_id(text, int, bigint, boolean, numeric);
DROP FUNCTION @extschema@.undo_partition_native(text, text, int, text, boolean, numeric);
DROP FUNCTION @extschema@.undo_partition(text, int, boolean, boolean, numeric);
DROP FUNCTION @extschema@.show_partitions(text, text);

CREATE FUNCTION @extschema@.autovacuum_off(p_parent_schema text, p_parent_tablename text, p_source_schema text DEFAULT NULL, p_source_tablename text DEFAULT NULL) RETURNS boolean
    LANGUAGE plpgsql
    AS $$
DECLARE

v_row       record;
v_sql       text;

BEGIN

    v_sql = format('ALTER TABLE %I.%I SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)', p_parent_schema, p_parent_tablename);
    RAISE DEBUG 'partition_data sql: %', v_sql;
    EXECUTE v_sql;

    IF p_source_tablename IS NOT NULL THEN
        v_sql = format('ALTER TABLE %I.%I SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)', p_source_schema, p_source_tablename);
        RAISE DEBUG 'partition_data sql: %', v_sql;
        EXECUTE v_sql;
    END IF;

    FOR v_row IN 
        SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_schema||'.'||p_parent_tablename, 'ASC')
    LOOP
        v_sql = format('ALTER TABLE %I.%I SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)', v_row.partition_schemaname, v_row.partition_tablename);
        RAISE DEBUG 'partition_data sql: %', v_sql;
        EXECUTE v_sql;
    END LOOP;

    RETURN true;

END
$$;


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



CREATE OR REPLACE FUNCTION @extschema@.apply_cluster(p_parent_schema text, p_parent_tablename text, p_child_schema text, p_child_tablename text) RETURNS void
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



CREATE OR REPLACE FUNCTION @extschema@.apply_publications(p_parent_table text, p_child_schema text, p_child_tablename text) RETURNS void
    LANGUAGE plpgsql 
AS $$
DECLARE
    v_publications      text[];
    v_row               record;
    v_sql               text;
BEGIN
/*
* Function to ATLER PUBLICATION ... ADD TABLE to support logical replication
*/

SELECT c.publications INTO v_publications
FROM @extschema@.part_config c
WHERE c.parent_table = p_parent_table;

-- Loop over all publicaions which the table needs to be added to
FOR v_row IN
    SELECT pubname FROM unnest(v_publications) AS pubname
LOOP
    v_sql = format('ALTER PUBLICATION %I ADD TABLE %I.%I', v_row.pubname, p_child_schema, p_child_tablename);
    RAISE DEBUG '%', v_sql;
    EXECUTE v_sql;
END LOOP;

END;
$$;



CREATE OR REPLACE FUNCTION @extschema@.check_control_type(p_parent_schema text, p_parent_tablename text, p_control text) RETURNS TABLE (general_type text, exact_type text) 
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



CREATE OR REPLACE FUNCTION @extschema@.check_parent(p_exact_count boolean DEFAULT true) RETURNS SETOF @extschema@.check_parent_table
    LANGUAGE plpgsql STABLE
    SET search_path = @extschema@,pg_temp
    AS $$
DECLARE

v_count             bigint = 0;
v_row               record;
v_schemaname        text;
v_tablename         text;
v_sql               text;
v_trouble           @extschema@.check_parent_table%rowtype;

BEGIN
/*
 * Function to monitor for data getting inserted into parent tables managed by extension
 */

FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config
LOOP
    SELECT schemaname, tablename 
    INTO v_schemaname, v_tablename
    FROM pg_catalog.pg_tables 
    WHERE schemaname = split_part(v_row.parent_table, '.', 1)::name
    AND tablename = split_part(v_row.parent_table, '.', 2)::name;

    IF p_exact_count THEN
        v_sql := format('SELECT count(1) AS n FROM ONLY %I.%I', v_schemaname, v_tablename);
    ELSE
        v_sql := format('SELECT count(1) AS n FROM (SELECT 1 FROM ONLY %I.%I LIMIT 1) x', v_schemaname, v_tablename);
    END IF;
    EXECUTE v_sql INTO v_count;

    IF v_count > 0 THEN 
        v_trouble.parent_table := v_schemaname ||'.'|| v_tablename;
        v_trouble.count := v_count;
        RETURN NEXT v_trouble;
    END IF;

    v_count := 0;

END LOOP;

RETURN;

END
$$;



CREATE OR REPLACE FUNCTION @extschema@.check_subpart_sameconfig(p_parent_table text) 
    RETURNS TABLE (sub_partition_type text
        , sub_control text
        , sub_partition_interval text
        , sub_constraint_cols text[]
        , sub_premake int
        , sub_inherit_fk boolean
        , sub_retention text
        , sub_retention_schema text
        , sub_retention_keep_table boolean
        , sub_retention_keep_index boolean
        , sub_automatic_maintenance text
        , sub_epoch text 
        , sub_optimize_trigger int
        , sub_optimize_constraint int
        , sub_infinite_time_partitions boolean
        , sub_jobmon boolean
        , sub_trigger_exception_handling boolean
        , sub_upsert text
        , sub_trigger_return_null boolean)
    LANGUAGE sql STABLE
    SET search_path = @extschema@,pg_temp
AS $$
/*
 * Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
 * Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
 * This is called by run_maintainance() and at least provides a consistent way to check that I know will run. 
 * If anyone can get a working constraint/trigger, please help!
*/

    WITH parent_info AS (
        SELECT c1.oid
        FROM pg_catalog.pg_class c1 
        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
        WHERE n1.nspname = split_part(p_parent_table, '.', 1)::name
        AND c1.relname = split_part(p_parent_table, '.', 2)::name
    )
    , child_tables AS (
        SELECT n.nspname||'.'||c.relname AS tablename
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        JOIN parent_info pi ON h.inhparent = pi.oid
    )
    -- Column order here must match the RETURNS TABLE definition
    SELECT DISTINCT a.sub_partition_type
        , a.sub_control
        , a.sub_partition_interval
        , a.sub_constraint_cols
        , a.sub_premake
        , a.sub_inherit_fk
        , a.sub_retention
        , a.sub_retention_schema
        , a.sub_retention_keep_table
        , a.sub_retention_keep_index
        , a.sub_automatic_maintenance
        , a.sub_epoch
        , a.sub_optimize_trigger
        , a.sub_optimize_constraint
        , a.sub_infinite_time_partitions
        , a.sub_jobmon
        , a.sub_trigger_exception_handling
        , a.sub_upsert
        , a.sub_trigger_return_null
    FROM @extschema@.part_config_sub a
    JOIN child_tables b on a.sub_parent = b.tablename;
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_function_id(p_parent_table text, p_job_id bigint DEFAULT NULL) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_control                       text;
v_control_type                  text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_function_name                 text;
v_higher_parent_schema          text := split_part(p_parent_table, '.', 1);
v_higher_parent_table           text := split_part(p_parent_table, '.', 2);
v_id_position                   int;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_max                           bigint;
v_next_partition_id             bigint;
v_next_partition_name           text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_optimize_trigger              int;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_interval            bigint;
v_premake                       int;
v_prev_partition_id             bigint;
v_prev_partition_name           text;
v_relkind                       char;
v_row_max_id                    record;
v_run_maint                     text;
v_step_id                       bigint;
v_top_parent                    text := p_parent_table;
v_trig_func                     text;
v_trigger_exception_handling    boolean;
v_trigger_return_null           boolean;
v_upsert                        text;

BEGIN
/*
 * Create the trigger function for the parent table of an id-based partition set
 */

SELECT partition_interval::bigint
    , control
    , premake
    , optimize_trigger
    , automatic_maintenance
    , jobmon
    , trigger_exception_handling
    , upsert
    , trigger_return_null
INTO v_partition_interval
    , v_control
    , v_premake
    , v_optimize_trigger
    , v_run_maint
    , v_jobmon
    , v_trigger_exception_handling
    , v_upsert
    , v_trigger_return_null
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND partition_type = 'partman';

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no non-native pg_partman config found for %', p_parent_table;
END IF;

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

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'id' THEN
    RAISE EXCEPTION 'Cannot run create_function_id on partition set without id based control column. Found: %', v_control_type;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN CREATE FUNCTION: %s', p_parent_table));
    ELSE
        v_job_id = p_job_id;
    END IF;
    v_step_id := add_step(v_job_id, format('Creating partition function for table %s', p_parent_table));
END IF;

-- Get the highest level top parent if multi-level partitioned in order to get proper max() value below
WHILE v_higher_parent_table IS NOT NULL LOOP -- initially set in DECLARE
    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_inherits i
        JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = v_higher_parent_schema::name
        AND c.relname = v_higher_parent_table::name
    ) SELECT n.nspname, c.relname
    INTO v_higher_parent_schema, v_higher_parent_table
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    JOIN top_oid t ON c.oid = t.top_parent_oid
    JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
    WHERE p.partition_type = 'id';

    IF v_higher_parent_table IS NOT NULL THEN
        -- initially set in DECLARE
        v_top_parent := v_higher_parent_schema||'.'||v_higher_parent_table;
    END IF;

END LOOP;

-- Loop through child tables starting from highest to get current max value in partition set
-- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
FOR v_row_max_id IN
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_top_parent, 'DESC')
LOOP
        EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max_id.partition_schemaname, v_row_max_id.partition_tablename) INTO v_max;
        IF v_max IS NOT NULL THEN
            EXIT;
        END IF;
END LOOP;
IF v_max IS NULL THEN
    v_max := 0;
END IF;
v_current_partition_id = v_max - (v_max % v_partition_interval);
v_next_partition_id := v_current_partition_id + v_partition_interval;
v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_current_partition_id::text, TRUE);

v_trig_func := format('CREATE OR REPLACE FUNCTION %I.%I() RETURNS trigger LANGUAGE plpgsql AS $t$ 
    DECLARE
        v_count                     int;
        v_current_partition_id      bigint;
        v_current_partition_name    text;
        v_id_position               int;
        v_last_partition            text := %L;
        v_next_partition_id         bigint;
        v_next_partition_name       text;
        v_partition_created         boolean;
    BEGIN
    IF TG_OP = ''INSERT'' THEN 
        IF NEW.%I >= %s AND NEW.%I < %s THEN '
            , v_parent_schema
            , v_function_name
            , v_last_partition
            , v_control
            , v_current_partition_id
            , v_control
            , v_next_partition_id
        );
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_current_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func || format(' 
            INSERT INTO %I.%I VALUES (NEW.*) %s; ', v_parent_schema, v_current_partition_name, v_upsert);
        ELSE
            v_trig_func := v_trig_func || '
            -- Child table for current values does not exist in this partition set, so write to parent
            RETURN NEW;';
        END IF;

    FOR i IN 1..v_optimize_trigger LOOP
        v_prev_partition_id := v_current_partition_id - (v_partition_interval * i);
        v_next_partition_id := v_current_partition_id + (v_partition_interval * i);
        v_final_partition_id := v_next_partition_id + v_partition_interval;
        v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, v_prev_partition_id::text, TRUE);
        v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, v_next_partition_id::text, TRUE);

        -- Check that child table exist before making a rule to insert to them.
        -- Handles optimize_trigger being larger than premake (to go back in time further) and edge case of changing optimize_trigger immediately after running create_parent().
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_prev_partition_name::name;
        IF v_count > 0 THEN
            -- Only handle previous partitions if they're starting above zero
            IF v_prev_partition_id >= 0 THEN
                v_trig_func := v_trig_func ||format('
        ELSIF NEW.%I >= %s AND NEW.%I < %s THEN 
            INSERT INTO %I.%I VALUES (NEW.*) %s; '
                , v_control
                , v_prev_partition_id
                , v_control
                , v_prev_partition_id + v_partition_interval
                , v_parent_schema
                , v_prev_partition_name
                , v_upsert
            );
            END IF;
        END IF;

        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_next_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||format('
        ELSIF NEW.%I >= %s AND NEW.%I < %s THEN 
            INSERT INTO %I.%I VALUES (NEW.*) %s;'
                , v_control
                , v_next_partition_id
                , v_control
                , v_final_partition_id
                , v_parent_schema
                , v_next_partition_name
                , v_upsert
            );
        END IF;
    END LOOP;
    v_trig_func := v_trig_func ||format('
        ELSE
            v_current_partition_id := NEW.%I - (NEW.%I %% %s);
            v_current_partition_name := @extschema@.check_name_length(%L, v_current_partition_id::text, TRUE);
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = %L::name AND tablename = v_current_partition_name::name;
            IF v_count > 0 THEN 
                EXECUTE format(''INSERT INTO %%I.%%I VALUES($1.*) %s'', %L, v_current_partition_name) USING NEW;
            ELSE
                RETURN NEW;
            END IF;
        END IF;'
            , v_control
            , v_control
            , v_partition_interval
            , v_parent_tablename
            , v_parent_schema
            , v_upsert
            , v_parent_schema
        );

    v_trig_func := v_trig_func ||'
    END IF;';

    IF v_trigger_return_null IS TRUE THEN
        v_trig_func := v_trig_func ||'
    RETURN NULL;';
    ELSE
        v_trig_func := v_trig_func ||'
    RETURN NEW;';
    END IF;

    IF v_trigger_exception_handling THEN 
        v_trig_func := v_trig_func ||'
    EXCEPTION WHEN OTHERS THEN
        RAISE WARNING ''pg_partman insert into child table failed, row inserted into parent (%.%). ERROR: %'', TG_TABLE_SCHEMA, TG_TABLE_NAME, COALESCE(SQLERRM, ''unknown'');
        RETURN NEW;';
    END IF;

    v_trig_func := v_trig_func ||'
    END $t$;';

EXECUTE v_trig_func;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', format('Added function for current id interval: %s to %s', v_current_partition_id, v_final_partition_id-1));
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE FUNCTION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition function maintenance for table %s failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_function_time(p_parent_table text, p_job_id bigint DEFAULT NULL) RETURNS void
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_control                       text;
v_control_type                  text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_timestamp   timestamptz;
v_datetime_string               text;
v_epoch                         text;
v_final_partition_timestamp     timestamptz;
v_function_name                 text;
v_infinite_time_partitions      boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_new_length                    int;
v_next_partition_name           text;
v_next_partition_timestamp      timestamptz;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_expression          text;
v_partition_interval            interval;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamptz;
v_relkind                       char;
v_row_max_time                  record;
v_step_id                       bigint;
v_trig_func                     text;
v_optimize_trigger              int;
v_table_exists                  boolean;
v_trigger_exception_handling    boolean;
v_trigger_return_null           boolean;
v_type                          text;
v_upsert                        text;

BEGIN
/*
 * Create the trigger function for the parent table of a time-based partition set
 */

SELECT partition_type
    , partition_interval::interval
    , epoch
    , control
    , optimize_trigger
    , datetime_string
    , jobmon
    , trigger_exception_handling
    , upsert
    , trigger_return_null
    , infinite_time_partitions
INTO v_type
    , v_partition_interval
    , v_epoch
    , v_control
    , v_optimize_trigger
    , v_datetime_string
    , v_jobmon
    , v_trigger_exception_handling
    , v_upsert
    , v_trigger_return_null
    , v_infinite_time_partitions
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (partition_type = 'partman' OR partition_type = 'time-custom');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no non-native pg_partman config found for %', p_parent_table;
END IF;

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

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'time' THEN 
    IF (v_control_type = 'id' AND v_epoch = 'none') OR v_control_type <> 'id' THEN
        RAISE EXCEPTION 'Cannot run on partition set without time based control column or epoch flag set with an id column. Found control: %, epoch: %', v_control_type, v_epoch;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN CREATE FUNCTION: %s', p_parent_table));
    ELSE
        v_job_id = p_job_id;
    END IF;
    v_step_id := add_step(v_job_id, format('Creating partition function for table %s', p_parent_table));
END IF;

IF v_infinite_time_partitions IS TRUE THEN
    -- Set it to "now" to line up with maintenance always making new partitions despite no new data
    -- Also, don't need to bother getting the max value in the partitions
    v_current_partition_timestamp := CURRENT_TIMESTAMP;
ELSE 

    v_partition_expression := CASE
        WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
        WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
        ELSE format('%I', v_control)
    END;

    FOR v_row_max_time IN
        SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
    LOOP
        EXECUTE format('SELECT max(%s)::text FROM %I.%I'
                            , v_partition_expression
                            , v_row_max_time.partition_schemaname
                            , v_row_max_time.partition_tablename
                        ) INTO v_current_partition_timestamp;

        IF v_current_partition_timestamp IS NOT NULL THEN
            EXIT;
        END IF;
    END LOOP;
    IF v_current_partition_timestamp IS NULL THEN
        v_current_partition_timestamp := CURRENT_TIMESTAMP;
    END IF;
END IF; -- end infinite time check

-- Reset for use in trigger function
v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(NEW.%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((NEW.%I/1000)::float)', v_control)
    ELSE format('NEW.%I', v_control)
END;

IF v_type = 'partman' THEN
    v_trig_func := format('CREATE OR REPLACE FUNCTION %I.%I() RETURNS trigger LANGUAGE plpgsql AS $t$
            DECLARE
            v_count                 int;
            v_partition_name        text;
            v_partition_timestamp   timestamptz;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            '
    , v_parent_schema
    , v_function_name);

    SELECT suffix_timestamp, partition_table, table_exists
    INTO v_current_partition_timestamp, v_current_partition_name, v_table_exists
    FROM @extschema@.show_partition_name(p_parent_table, v_current_partition_timestamp::text);

    CASE
        WHEN v_partition_interval = '15 mins' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''hour'', %s) +
                ''15min''::interval * floor(date_part(''minute'', %1$s) / 15.0);' , v_partition_expression);
        WHEN v_partition_interval = '30 mins' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''hour'', %s) +
                ''30min''::interval * floor(date_part(''minute'', %1$s) / 30.0);' , v_partition_expression);
        WHEN v_partition_interval = '1 hour' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''hour'', %s);', v_partition_expression);
        WHEN v_partition_interval = '1 day' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''day'', %s);', v_partition_expression);
        WHEN v_partition_interval = '1 week' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''week'', %s);', v_partition_expression);
        WHEN v_partition_interval = '1 month' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''month'', %s);', v_partition_expression);
        WHEN v_partition_interval = '3 months' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''quarter'', %s);', v_partition_expression);
        WHEN v_partition_interval = '1 year' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''year'', %s);', v_partition_expression);
    END CASE;

    v_next_partition_timestamp := v_current_partition_timestamp + v_partition_interval::interval;

    v_trig_func := v_trig_func ||format('
            IF %s >= %L AND %1$s < %3$L THEN '
            , v_partition_expression
            , v_current_partition_timestamp
            , v_next_partition_timestamp);

    IF v_table_exists THEN
        v_trig_func := v_trig_func || format('
            INSERT INTO %I.%I VALUES (NEW.*) %s; ', v_parent_schema, v_current_partition_name, v_upsert);
    ELSE
        v_trig_func := v_trig_func || '
            -- Child table for current values does not exist in this partition set, so write to parent
            RETURN NEW;';
    END IF;

    FOR i IN 1..v_optimize_trigger LOOP
        v_prev_partition_timestamp := v_current_partition_timestamp - (v_partition_interval::interval * i);
        v_next_partition_timestamp := v_current_partition_timestamp + (v_partition_interval::interval * i);
        v_final_partition_timestamp := v_next_partition_timestamp + (v_partition_interval::interval);
        v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, to_char(v_prev_partition_timestamp, v_datetime_string), TRUE);
        v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, to_char(v_next_partition_timestamp, v_datetime_string), TRUE);

        -- Check that child table exist before making a rule to insert to them.
        -- Handles optimize_trigger being larger than premake (to go back in time further) and edge case of changing optimize_trigger immediately after running create_parent().
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_prev_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||format('
            ELSIF %s >= %L AND %1$s < %3$L THEN 
                INSERT INTO %I.%I VALUES (NEW.*) %s;'
                , v_partition_expression
                , v_prev_partition_timestamp
                , v_prev_partition_timestamp + v_partition_interval::interval
                , v_parent_schema
                , v_prev_partition_name
                , v_upsert);
        END IF;
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_next_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||format(' 
            ELSIF %s >= %L AND %1$s < %3$L THEN 
                INSERT INTO %I.%I VALUES (NEW.*) %s;'
                , v_partition_expression
                , v_next_partition_timestamp
                , v_final_partition_timestamp
                , v_parent_schema
                , v_next_partition_name
                , v_upsert);
        END IF;

    END LOOP;

    v_trig_func := v_trig_func||format('
            ELSE
                v_partition_name := @extschema@.check_name_length(%L, to_char(v_partition_timestamp, %L), TRUE);
                SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = %L::name AND tablename = v_partition_name::name;
                IF v_count > 0 THEN 
                    EXECUTE format(''INSERT INTO %%I.%%I VALUES($1.*) %s'', %L, v_partition_name) USING NEW;
                ELSE
                    RETURN NEW;
                END IF;
            END IF;'
            , v_parent_tablename
            , v_datetime_string
            , v_parent_schema
            , v_upsert
            , v_parent_schema);

    v_trig_func := v_trig_func ||'
        END IF;';

    IF v_trigger_return_null IS TRUE THEN
        v_trig_func := v_trig_func ||'
        RETURN NULL;';
    ELSE
        v_trig_func := v_trig_func ||'
        RETURN NEW;';
    END IF;

    IF v_trigger_exception_handling THEN 
        v_trig_func := v_trig_func ||'
        EXCEPTION WHEN OTHERS THEN
            RAISE WARNING ''pg_partman insert into child table failed, row inserted into parent (%.%). ERROR: %'', TG_TABLE_SCHEMA, TG_TABLE_NAME, COALESCE(SQLERRM, ''unknown'');
            RETURN NEW;';
    END IF;
    v_trig_func := v_trig_func ||'
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Added function for current time interval: %s to %s' 
                                                        , v_current_partition_timestamp
                                                        , v_final_partition_timestamp-'1sec'::interval));
    END IF;

ELSIF v_type = 'time-custom' THEN

    v_trig_func := format('CREATE OR REPLACE FUNCTION %I.%I() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_child_schemaname  text;
            v_child_table       text;
            v_child_tablename   text;
            v_upsert            text;
        BEGIN
            '
        , v_parent_schema
        , v_function_name);

    v_trig_func := v_trig_func || format(' 
        SELECT c.child_table, p.upsert INTO v_child_table, v_upsert
        FROM @extschema@.custom_time_partitions c
        JOIN @extschema@.part_config p ON c.parent_table = p.parent_table
        WHERE c.partition_range @> %s 
        AND c.parent_table = %L;'
        , v_partition_expression
        , v_parent_schema||'.'||v_parent_tablename);

    v_trig_func := v_trig_func || '
        SELECT schemaname, tablename INTO v_child_schemaname, v_child_tablename 
        FROM pg_catalog.pg_tables 
        WHERE schemaname = split_part(v_child_table, ''.'', 1)::name
        AND tablename = split_part(v_child_table, ''.'', 2)::name;
        IF v_child_schemaname IS NOT NULL AND v_child_tablename IS NOT NULL THEN
            EXECUTE format(''INSERT INTO %I.%I VALUES ($1.*) %s'', v_child_schemaname, v_child_tablename, v_upsert) USING NEW;
        ELSE
            RETURN NEW;
        END IF;';

    IF v_trigger_return_null IS TRUE THEN
        v_trig_func := v_trig_func ||'
        RETURN NULL;';
    ELSE
        v_trig_func := v_trig_func ||'
        RETURN NEW;';
    END IF;

    IF v_trigger_exception_handling THEN 
        v_trig_func := v_trig_func ||'
        EXCEPTION WHEN OTHERS THEN
            RAISE WARNING ''pg_partman insert into child table failed, row inserted into parent (%.%). ERROR: %'', TG_TABLE_SCHEMA, TG_TABLE_NAME, COALESCE(SQLERRM, ''unknown'');
            RETURN NEW;';
    END IF;
    v_trig_func := v_trig_func ||'
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Added function for custom time table: %s', p_parent_table));
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid time partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE FUNCTION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition function maintenance for table %s failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_automatic_maintenance text DEFAULT 'on' 
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch text DEFAULT 'none' 
    , p_upsert text DEFAULT ''
    , p_publications text[] DEFAULT NULL
    , p_trigger_return_null boolean DEFAULT true
    , p_template_table text DEFAULT NULL
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean 
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_partattrs                     smallint[];
v_base_timestamp                timestamptz;
v_count                         int := 1;
v_control_type                  text;
v_control_exact_type            text;
v_datetime_string               text;
v_default_partition             text;
v_higher_control_type           text;
v_higher_parent_control         text;
v_higher_parent_schema          text := split_part(p_parent_table, '.', 1);
v_higher_parent_table           text := split_part(p_parent_table, '.', 2);
v_id_interval                   bigint;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_created        boolean;
v_max                           bigint;
v_native_sub_control            text;
v_notnull                       boolean;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_parent_owner                  text;
v_parent_partition_id           bigint;
v_parent_partition_timestamp    timestamptz;
v_parent_schema                 text;
v_parent_tablename              text;
v_parent_tablespace             text;
v_part_col                      text;
v_part_type                     text;
v_partition_time                timestamptz;
v_partition_time_array          timestamptz[];
v_partition_id_array            bigint[];
v_partstrat                     char;
v_publication_exists            text;
v_row                           record;
v_sql                           text;
v_start_time                    timestamptz;
v_starting_partition_id         bigint;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_parent                    text;
v_success                       boolean := false;
v_template_schema               text;
v_template_tablename            text;
v_time_interval                 interval;
v_top_datetime_string           text;
v_top_parent_schema             text := split_part(p_parent_table, '.', 1);
v_top_parent_table              text := split_part(p_parent_table, '.', 2);
v_unlogged                      char;

BEGIN
/*
 * Function to turn a table into the parent of a partition set
 */

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

IF p_upsert <> '' THEN
    IF current_setting('server_version_num')::int < 90500 THEN
        RAISE EXCEPTION 'INSERT ... ON CONFLICT (UPSERT) feature is only supported in PostgreSQL 9.5 and later';
    END IF;
    IF p_type = 'native' THEN
        RAISE EXCEPTION 'Native partitioning does not currently support upsert. Use pg_partman''s partitioning methods instead if this is required';
    END IF;
END IF;

SELECT n.nspname, c.relname, t.spcname, c.relpersistence
INTO v_parent_schema, v_parent_tablename, v_parent_tablespace, v_unlogged
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Please create parent table first: %', p_parent_table;
    END IF;
    
SELECT attnotnull INTO v_notnull 
FROM pg_catalog.pg_attribute a
JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE c.relname = v_parent_tablename::name
AND n.nspname = v_parent_schema::name
AND a.attname = p_control::name;
    IF p_type <> 'native' AND (v_notnull = false OR v_notnull IS NULL) THEN
        RAISE EXCEPTION 'Control column given (%) for parent table (%) does not exist or must be set to NOT NULL', p_control, p_parent_table;
    END IF;

SELECT general_type, exact_type INTO v_control_type, v_control_exact_type
FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, p_control);

IF (p_epoch <> 'none' AND v_control_type <> 'id') THEN
    RAISE EXCEPTION 'p_epoch can only be used with an integer based control column and does not work for native partitioning';
END IF;


IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type for pg_partman', p_type;
END IF;

IF p_type = 'native' THEN

    IF current_setting('server_version_num')::int < 100000 THEN
        RAISE EXCEPTION 'Native partitioning only available in PostgreSQL versions 10.0+';
    END IF;
    -- Check if given parent table has been already set up as a partitioned table and is ranged
    SELECT p.partstrat, partattrs INTO v_partstrat, v_partattrs
    FROM pg_catalog.pg_partitioned_table p
    JOIN pg_catalog.pg_class c ON p.partrelid = c.oid
    JOIN pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_parent_schema::name 
    AND c.relname = v_parent_tablename::name;

    IF v_partstrat <> 'r' OR v_partstrat IS NULL THEN
        RAISE EXCEPTION 'When using native partitioning, you must have created the given parent table as ranged (not list) partitioned already. Ex: CREATE TABLE ... PARITIONED BY RANGE ...)';
    END IF;

    IF array_length(v_partattrs, 1) > 1 THEN
        RAISE NOTICE 'pg_partman only supports single column native partitioning at this time. Found % columns in given parent definition.', array_length(v_partattrs, 1);
    END IF;

    SELECT a.attname, t.typname
    INTO v_part_col, v_part_type
    FROM pg_attribute a
    JOIN pg_class c ON a.attrelid = c.oid
    JOIN pg_namespace n ON c.relnamespace = n.oid
    JOIN pg_type t ON a.atttypid = t.oid
    WHERE n.nspname = v_parent_schema::name
    AND c.relname = v_parent_tablename::name
    AND attnum IN (SELECT unnest(partattrs) FROM pg_partitioned_table p WHERE a.attrelid = p.partrelid);

    IF p_control <> v_part_col OR v_control_exact_type <> v_part_type THEN
        RAISE EXCEPTION 'Control column and type given in arguments (%, %) does not match the control column and type of the given native partition set (%, %)', p_control, v_control_exact_type, v_part_col, v_part_type;
    END IF;

    -- Check that control column is a usable type for pg_partman.
    IF v_control_type NOT IN ('time', 'id') THEN
        RAISE EXCEPTION 'Only date/time or integer types are allowed for the control column with native partitioning.';
    END IF;

    -- Table to handle properties not natively inherited yet (indexes, fks, etc)
    IF p_template_table IS NULL THEN
        v_template_schema := '@extschema@';
        v_template_tablename := @extschema@.check_name_length('template_'||v_parent_schema||'_'||v_parent_tablename);
        EXECUTE format('CREATE TABLE IF NOT EXISTS %I.%I (LIKE %I.%I)', '@extschema@', v_template_tablename, v_parent_schema, v_parent_tablename);

        SELECT pg_get_userbyid(c.relowner) INTO v_parent_owner 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema::name 
        AND c.relname = v_parent_tablename::name;

        EXECUTE format('ALTER TABLE %I.%I OWNER TO %I'
                , '@extschema@' 
                , v_template_tablename 
                , v_parent_owner);
    ELSE
        SELECT n.nspname, c.relname INTO v_template_schema, v_template_tablename
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = split_part(p_template_table, '.', 1)::name
        AND c.relname = split_part(p_template_table, '.', 2)::name;
            IF v_template_tablename IS NULL THEN
                RAISE EXCEPTION 'Unable to find given template table in system catalogs (%). Please create template table first or leave parameter NULL to have a default one created for you.', p_parent_table;
            END IF;
    END IF;

ELSE

    IF current_setting('server_version_num')::int >= 100000 THEN
        SELECT p.partstrat INTO v_partstrat
        FROM pg_catalog.pg_partitioned_table p
        JOIN pg_catalog.pg_class c ON p.partrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema::name 
        AND c.relname = v_parent_tablename::name;
    END IF;

    IF v_partstrat IS NOT NULL THEN
        RAISE EXCEPTION 'Given parent table has been set up with native partitioning therefore cannot be used with pg_partman''s other partitioning types. Either recreate table non-native or set the type argument to ''native''';
    END IF;

END IF;

IF p_publications IS NOT NULL THEN
    IF current_setting('server_version_num')::int < 100000 THEN
        RAISE EXCEPTION 'p_publications argument not null but CREATE PUBLICATION is only available in PostgreSQL versions 10.0+';
    END IF;
    IF p_publications = '{}' THEN
        RAISE EXCEPTION 'p_publications cannot be an empty set';
    END IF;
    FOR v_row IN 
        SELECT unnest(p_publications) AS pubname
    LOOP
        SELECT pubname INTO v_publication_exists FROM pg_catalog.pg_publication where pubname = v_row.pubname::name;
        IF v_publication_exists IS NULL THEN
            RAISE EXCEPTION 'Given publication name (%) does not exist in system catalog. Ensure it is created first.', v_row.pubname;
        END IF;
    END LOOP;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

EXECUTE format('LOCK TABLE %I.%I IN ACCESS EXCLUSIVE MODE', v_parent_schema, v_parent_tablename);

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN SETUP PARENT: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Creating initial partitions on new parent table: %s', p_parent_table));
END IF;

-- If this parent table has siblings that are also partitioned (subpartitions), ensure this parent gets added to part_config_sub table so future maintenance will subpartition it
-- Just doing in a loop to avoid having to assign a bunch of variables (should only run once, if at all; constraint should enforce only one value.)
FOR v_row IN 
    WITH parent_table AS (
        SELECT h.inhparent AS parent_oid
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON h.inhrelid = c.oid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = v_parent_tablename::name
        AND n.nspname = v_parent_schema::name
    ), sibling_children AS (
        SELECT i.inhrelid::regclass::text AS tablename 
        FROM pg_inherits i
        JOIN parent_table p ON i.inhparent = p.parent_oid
    )
    SELECT DISTINCT sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_automatic_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
        , sub_trigger_exception_handling
        , sub_upsert
        , sub_trigger_return_null
        , sub_template_table
    FROM @extschema@.part_config_sub a
    JOIN sibling_children b on a.sub_parent = b.tablename LIMIT 1
LOOP
    INSERT INTO @extschema@.part_config_sub (
        sub_parent
        , sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_automatic_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
        , sub_trigger_exception_handling
        , sub_upsert
        , sub_trigger_return_null
        , sub_template_table)
    VALUES (
        p_parent_table
        , v_row.sub_partition_type
        , v_row.sub_control
        , v_row.sub_partition_interval
        , v_row.sub_constraint_cols
        , v_row.sub_premake
        , v_row.sub_inherit_fk
        , v_row.sub_retention
        , v_row.sub_retention_schema
        , v_row.sub_retention_keep_table
        , v_row.sub_retention_keep_index
        , v_row.sub_automatic_maintenance
        , v_row.sub_epoch
        , v_row.sub_optimize_trigger
        , v_row.sub_optimize_constraint
        , v_row.sub_infinite_time_partitions
        , v_row.sub_jobmon
        , v_row.sub_trigger_exception_handling
        , v_row.sub_upsert
        , v_row.sub_trigger_return_null
        , v_row.sub_template_table);
    
END LOOP;

IF v_control_type = 'time' OR (v_control_type = 'id' AND p_epoch <> 'none') THEN

    CASE
        WHEN p_interval = 'yearly' THEN
            v_time_interval := '1 year';
        WHEN p_interval = 'quarterly' THEN
            v_time_interval := '3 months';
        WHEN p_interval = 'monthly' THEN
            v_time_interval := '1 month';
        WHEN p_interval  = 'weekly' THEN
            v_time_interval := '1 week';
        WHEN p_interval = 'daily' THEN
            v_time_interval := '1 day';
        WHEN p_interval = 'hourly' THEN
            v_time_interval := '1 hour';
        WHEN p_interval = 'half-hour' THEN
            v_time_interval := '30 mins';
        WHEN p_interval = 'quarter-hour' THEN
            v_time_interval := '15 mins';
        ELSE
            IF p_type <> 'native' THEN
                -- Reset for use as part_config type value below
                p_type = 'time-custom';
            END IF;
            v_time_interval := p_interval::interval;
            IF v_time_interval < '1 second'::interval THEN
                RAISE EXCEPTION 'Partitioning interval must be 1 second or greater';
            END IF;
    END CASE;

   -- First partition is either the min premake or p_start_partition
    v_start_time := COALESCE(p_start_partition::timestamptz, CURRENT_TIMESTAMP - (v_time_interval * p_premake));

    IF v_time_interval >= '1 year' THEN
        v_base_timestamp := date_trunc('year', v_start_time);
        IF v_time_interval >= '10 years' THEN
            v_base_timestamp := date_trunc('decade', v_start_time);
            IF v_time_interval >= '100 years' THEN
                v_base_timestamp := date_trunc('century', v_start_time);
                IF v_time_interval >= '1000 years' THEN
                    v_base_timestamp := date_trunc('millennium', v_start_time);
                END IF; -- 1000
            END IF; -- 100
        END IF; -- 10
    END IF; -- 1

    v_datetime_string := 'YYYY';
    IF v_time_interval < '1 year' THEN
        IF p_interval = 'quarterly' THEN
            v_base_timestamp := date_trunc('quarter', v_start_time);
            v_datetime_string = 'YYYY"q"Q';
        ELSE
            v_base_timestamp := date_trunc('month', v_start_time); 
            v_datetime_string := v_datetime_string || '_MM';
        END IF;
        IF v_time_interval < '1 month' THEN
            IF p_interval = 'weekly' THEN
                v_base_timestamp := date_trunc('week', v_start_time);
                v_datetime_string := 'IYYY"w"IW';
            ELSE 
                v_base_timestamp := date_trunc('day', v_start_time);
                v_datetime_string := v_datetime_string || '_DD';
            END IF;
            IF v_time_interval < '1 day' THEN
                v_base_timestamp := date_trunc('hour', v_start_time);
                v_datetime_string := v_datetime_string || '_HH24MI';
                IF v_time_interval < '1 minute' THEN
                    v_base_timestamp := date_trunc('minute', v_start_time);
                    v_datetime_string := v_datetime_string || 'SS';
                END IF; -- minute
            END IF; -- day
        END IF; -- month
    END IF; -- year

    v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
    LOOP
        -- If current loop value is less than or equal to the value of the max premake, add time to array.
        IF (v_base_timestamp + (v_time_interval * v_count)) < (CURRENT_TIMESTAMP + (v_time_interval * p_premake)) THEN
            BEGIN
                v_partition_time := (v_base_timestamp + (v_time_interval * v_count))::timestamptz;
                v_partition_time_array := array_append(v_partition_time_array, v_partition_time);
            EXCEPTION WHEN datetime_field_overflow THEN
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                    Child partition creation after time % skipped', v_partition_time;
                v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_partition_time||' skipped');
                CONTINUE;
            END;
        ELSE
            EXIT; -- all needed partitions added to array. Exit the loop.
        END IF;
        v_count := v_count + 1;
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , epoch
        , control
        , premake
        , constraint_cols
        , datetime_string
        , automatic_maintenance
        , inherit_fk
        , jobmon 
        , upsert
        , trigger_return_null
        , template_table
        , publications)
    VALUES (
        p_parent_table
        , p_type
        , v_time_interval
        , p_epoch
        , p_control
        , p_premake
        , p_constraint_cols
        , v_datetime_string
        , p_automatic_maintenance
        , p_inherit_fk
        , p_jobmon
        , p_upsert
        , p_trigger_return_null
        , v_template_schema||'.'||v_template_tablename
        , p_publications); 

    RAISE DEBUG 'create_parent: v_partition_time_array: %', v_partition_time_array;

    v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);

    IF v_last_partition_created = false THEN 
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- First see if this parent is a subpartition managed by pg_partman
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename::name
            AND n.nspname = v_parent_schema::name
        ) SELECT n.nspname, c.relname 
        INTO v_top_parent_schema, v_top_parent_table 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_top_parent_table IS NOT NULL THEN
            -- If so create the lowest possible partition that is within the boundary of the parent
            SELECT child_start_time INTO v_parent_partition_timestamp FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_schema||'.'||v_top_parent_table);
            IF v_base_timestamp >= v_parent_partition_timestamp THEN
                WHILE v_base_timestamp >= v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp - v_time_interval;
                END LOOP;
                v_base_timestamp := v_base_timestamp + v_time_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_base_timestamp < v_parent_partition_timestamp THEN
                WHILE v_base_timestamp < v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp + v_time_interval;
                END LOOP;
                -- Don't need to remove one since new starting time will fit in top parent interval
            END IF;
            v_partition_time_array := NULL;
            v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
            v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);
        ELSE
            RAISE WARNING 'No child tables created. Check that all child tables did not already exist and may not have been part of partition set. Given parent has still been configured with pg_partman, but may not have expected children. Please review schema and config to confirm things are ok.';

            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
                IF v_step_overflow_id IS NOT NULL THEN
                    PERFORM fail_job(v_job_id);
                ELSE
                    PERFORM close_job(v_job_id);
                END IF;
            END IF;

            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

            RETURN v_success;
        END IF; 
    END IF; -- End v_last_partition IF

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Time partitions premade: %s', p_premake));
    END IF;

END IF;

IF v_control_type = 'id' AND p_epoch = 'none' THEN
    v_id_interval := p_interval::bigint;
    IF p_type <> 'native' AND v_id_interval < 10 THEN
        RAISE EXCEPTION 'Interval for serial, non-native partitioning must be greater than or equal to 10';
    END IF;

    -- Check if parent table is a subpartition of an already existing id partition set managed by pg_partman. 
    WHILE v_higher_parent_table IS NOT NULL LOOP -- initially set in DECLARE
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE n.nspname = v_higher_parent_schema::name
            AND c.relname = v_higher_parent_table::name
        ) SELECT n.nspname, c.relname, p.control
        INTO v_higher_parent_schema, v_higher_parent_table, v_higher_parent_control
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_higher_parent_table IS NOT NULL THEN
            SELECT general_type INTO v_higher_control_type
            FROM @extschema@.check_control_type(v_higher_parent_schema, v_higher_parent_table, v_higher_parent_control);
            IF v_higher_control_type <> 'id' THEN
                -- The parent above the p_parent_table parameter is not partitioned by ID
                --   so don't check for max values in parents that aren't partitioned by ID.
                -- This avoids missing child tables in subpartition sets that have differing ID data
                EXIT;
            END IF;
            -- v_top_parent initially set in DECLARE
            v_top_parent_schema := v_higher_parent_schema;
            v_top_parent_table := v_higher_parent_table;
        END IF;
    END LOOP;

    -- If custom start partition is set, use that.
    -- If custom start is not set and there is already data, start partitioning with the highest current value and ensure it's grabbed from highest top parent table
    IF p_start_partition IS NOT NULL THEN
        v_max := p_start_partition::bigint;
    ELSE
        v_sql := format('SELECT COALESCE(max(%I)::bigint, 0) FROM %I.%I LIMIT 1'
                    , p_control
                    , v_top_parent_schema
                    , v_top_parent_table);
        EXECUTE v_sql INTO v_max;
    END IF;

    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive (and custom start partition wasn't set)
        IF p_start_partition IS NULL AND 
            (v_starting_partition_id - (v_id_interval*i)) > 0 AND 
            (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id 
        THEN
            v_partition_id_array = array_append(v_partition_id_array, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id_array = array_append(v_partition_id_array, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , control
        , premake
        , constraint_cols
        , automatic_maintenance
        , inherit_fk
        , jobmon
        , upsert
        , trigger_return_null
        , template_table
        , publications)
    VALUES (
        p_parent_table
        , p_type
        , v_id_interval
        , p_control
        , p_premake
        , p_constraint_cols
        , p_automatic_maintenance 
        , p_inherit_fk
        , p_jobmon
        , p_upsert
        , p_trigger_return_null
        , v_template_schema||'.'||v_template_tablename
        , p_publications); 

    v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);

    IF v_last_partition_created = false THEN
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- See if it's actually a subpartition of a parent id partition
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename::name
            AND n.nspname = v_parent_schema::name
        ) SELECT n.nspname||'.'||c.relname
        INTO v_top_parent_table
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_top_parent_table IS NOT NULL THEN
            -- Create the lowest possible partition that is within the boundary of the parent
             SELECT child_start_id INTO v_parent_partition_id FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_table);
            IF v_starting_partition_id >= v_parent_partition_id THEN
                WHILE v_starting_partition_id >= v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id - v_id_interval;
                END LOOP;
                v_starting_partition_id := v_starting_partition_id + v_id_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_starting_partition_id < v_parent_partition_id THEN
                WHILE v_starting_partition_id < v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id + v_id_interval;
                END LOOP;
                -- Don't need to remove one since new starting id will fit in top parent interval
            END IF;
            v_partition_id_array = NULL;
            v_partition_id_array = array_append(v_partition_id_array, v_starting_partition_id);
            v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);
        ELSE
            -- Currently unknown edge case if code gets here
            RAISE WARNING 'No child tables created. Check that all child tables did not already exist and may not have been part of partition set. Given parent has still been configured with pg_partman, but may not have expected children. Please review schema and config to confirm things are ok.';
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
                IF v_step_overflow_id IS NOT NULL THEN
                    PERFORM fail_job(v_job_id);
                ELSE
                    PERFORM close_job(v_job_id);
                END IF;
            END IF;

            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

            RETURN v_success;
        END IF;
    END IF; -- End v_last_partition_created IF

END IF; -- End IF id

IF p_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
    -- Add default partition to native sets in PG11+

    v_default_partition := @extschema@.check_name_length(v_parent_tablename, '_default', FALSE);
    v_sql := 'CREATE'; 
    IF v_unlogged = 'u' THEN
        v_sql := v_sql ||' UNLOGGED';
    END IF;
    -- Same INCLUDING list is used in create_partition_*()
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS)'
        , v_parent_schema, v_default_partition, v_parent_schema, v_parent_tablename);
    EXECUTE v_sql;
    v_sql := format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I DEFAULT'
        , v_parent_schema, v_parent_tablename, v_parent_schema, v_default_partition);
    EXECUTE v_sql;

    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_default_partition, v_parent_tablespace);
    END IF;

    -- NOTE: Privileges currently not automatically inherited for native
    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_default_partition, v_job_id);
END IF;

IF p_type <> 'native' THEN
    IF v_jobmon_schema IS NOT NULL  THEN
        v_step_id := add_step(v_job_id, 'Creating partition function');
    END IF;
    IF v_control_type = 'time' OR (v_control_type = 'id' AND p_epoch <> 'none') THEN
        PERFORM @extschema@.create_function_time(p_parent_table, v_job_id);
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Time function created');
        END IF;
    ELSIF v_control_type = 'id' THEN
        PERFORM @extschema@.create_function_id(p_parent_table, v_job_id);  
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'ID function created');
        END IF;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating partition trigger');
    END IF;
    PERFORM @extschema@.create_trigger(p_parent_table);
END IF; -- end native check


IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

v_success := true;

RETURN v_success;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE PARENT: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition creation for table '||p_parent_table||' failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_partition_id(p_parent_table text, p_partition_ids bigint[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
v_all                   text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze               boolean := FALSE;
v_control               text;
v_control_type          text;
v_exists                text;
v_grantees              text[];
v_hasoids               boolean;
v_id                    bigint;
v_inherit_fk            boolean;
v_job_id                bigint;
v_jobmon                boolean;
v_jobmon_schema         text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_grant          record;
v_parent_schema         text;
v_parent_tablename      text;
v_parent_tablespace     text;
v_partition_interval    bigint;
v_partition_created     boolean := false;
v_partition_name        text;
v_partition_type        text;
v_publications          text[];
v_revoke                text;
v_row                   record;
v_sql                   text;
v_step_id               bigint;
v_sub_control           text;
v_sub_partition_type    text; 
v_sub_id_max            bigint;
v_sub_id_min            bigint;
v_template_table        text;
v_unlogged              char;

BEGIN
/*
 * Function to create id partitions
 */

SELECT control
    , partition_type
    , partition_interval
    , inherit_fk
    , jobmon
    , template_table
    , publications
INTO v_control
    , v_partition_type
    , v_partition_interval
    , v_inherit_fk
    , v_jobmon
    , v_template_table
    , v_publications
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT n.nspname, c.relname, t.spcname 
INTO v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name

AND c.relname = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'id' THEN
    RAISE EXCEPTION 'ERROR: Given parent table is not set up for id/serial partitioning';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'id');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

FOREACH v_id IN ARRAY p_partition_ids LOOP
-- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_id_min IS NOT NULL THEN
        IF v_id < v_sub_id_min OR v_id > v_sub_id_max THEN
            CONTINUE;
        END IF;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_id::text, TRUE);
    -- If child table already exists, skip creation
    -- Have to check pg_class because if subpartitioned, table will not be in pg_tables
    SELECT c.relname INTO v_exists 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_parent_schema::name AND c.relname = v_partition_name::name;
    IF v_exists IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + v_partition_interval)-1);
    END IF;

    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;

    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    -- Close parentheses on LIKE are below due to differing requirements of native subpartitioning
    -- Same INCLUDING list is used in create_parent()
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS '
            , v_parent_schema
            , v_partition_name
            , v_parent_schema
            , v_parent_tablename);

    SELECT sub_partition_type, sub_control INTO v_sub_partition_type, v_sub_control 
    FROM @extschema@.part_config_sub 
    WHERE sub_parent = p_parent_table;
    IF v_sub_partition_type = 'native' THEN
        -- NOTE: Need to handle this differently when index inheritance is supported natively
        -- Cannot include indexes since they cannot exist on native parents.
        v_sql := v_sql || format(') PARTITION BY RANGE (%I) ', v_sub_control);
    ELSE
        v_sql := v_sql || format(' INCLUDING INDEXES) ', v_sub_control);
    END IF;


    SELECT relhasoids INTO v_hasoids 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;

        IF v_partition_type = 'native' THEN

        IF v_template_table IS NOT NULL THEN
            PERFORM @extschema@.inherit_template_properties(p_parent_table, v_parent_schema, v_partition_name);
        END IF;

        EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
            , v_parent_schema
            , v_parent_tablename
            , v_parent_schema
            , v_partition_name
            , v_id
            , v_id + v_partition_interval);

    ELSE
        -- Handled in inherit_template_properties for native because CREATE TABLE ignores TABLESPACE flag for native partition parents
        IF v_parent_tablespace IS NOT NULL THEN
            EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
        END IF;

        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %s AND %I < %s )'
            , v_parent_schema
            , v_partition_name
            , v_partition_name||'_partition_check'
            , v_control
            , v_id
            , v_control
            , v_id + v_partition_interval);

        EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I', v_parent_schema, v_partition_name, v_parent_schema, v_parent_tablename);

        -- Indexes cannot be created on the parent, so clustering cannot be used for native yet.
        PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

        -- Foreign keys to other tables not supported on native parent tables
        IF v_inherit_fk THEN
            PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
        END IF;

    END IF;
    
    -- NOTE: Privileges currently not automatically inherited for native
    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_automatic_maintenance
            , sub_infinite_time_partitions
            , sub_jobmon
            , sub_trigger_exception_handling
            , sub_template_table
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Subpartitioning '||v_partition_name);
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_automatic_maintenance := %L
                , p_inherit_fk := %L
                , p_epoch := %L
                , p_template_table := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_automatic_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_template_table
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
            , trigger_exception_handling = v_row.sub_trigger_exception_handling
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;

    END LOOP; -- end sub partitioning LOOP
    
    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    IF v_publications IS NOT NULL THEN
        -- NOTE: Publications currently not supported on parent table, but are supported on the table partitions if individually assigned.
        PERFORM @extschema@.apply_publications(p_parent_table, v_parent_schema, v_partition_name);
    END IF;

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s', p_parent_table));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    PERFORM close_job(v_job_id);
END IF;
 
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint; 
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_partition_time(p_parent_table text, p_partition_times timestamptz[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) 
RETURNS boolean
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_control_type                  text;
v_datetime_string               text;
v_epoch                         text;
v_exists                        smallint;
v_grantees                      text[];
v_hasoids                       boolean;
v_inherit_fk                    boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_parent_grant                  record;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_col                      text;
v_partition_created             boolean := false;
v_partition_name                text;
v_partition_suffix              text;
v_parent_tablespace             text;
v_partition_expression          text;
v_partition_interval            interval;
v_partition_timestamp_end       timestamptz;
v_partition_timestamp_start     timestamptz;
v_publications                  text[];
v_quarter                       text;
v_revoke                        text;
v_row                           record;
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_control                   text;
v_sub_parent                    text;
v_sub_partition_type            text;
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_min             timestamptz;
v_template_table                text;
v_trunc_value                   text;
v_time                          timestamptz;
v_partition_type                          text;
v_unlogged                      char;
v_year                          text;

BEGIN
/*
 * Function to create a child table in a time-based partition set
 */

SELECT partition_type
    , control
    , partition_interval
    , epoch
    , inherit_fk
    , jobmon
    , datetime_string
    , template_table
    , publications
INTO v_partition_type
    , v_control
    , v_partition_interval
    , v_epoch
    , v_inherit_fk
    , v_jobmon
    , v_datetime_string
    , v_template_table
    , v_publications
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT n.nspname, c.relname, t.spcname 
INTO v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'time' THEN 
    IF (v_control_type = 'id' AND v_epoch = 'none') OR v_control_type <> 'id' THEN
        RAISE EXCEPTION 'Cannot run on partition set without time based control column or epoch flag set with an id column. Found control: %, epoch: %', v_control_type, v_epoch;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'time');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
    ELSE format('%I', v_control)
END;
IF p_debug THEN
    RAISE NOTICE 'create_partition_time: v_partition_expression: %', v_partition_expression;
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    
    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_partition_interval;
    EXCEPTION WHEN datetime_field_overflow THEN
        RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
            Child partition creation after time % skipped', v_time;
        v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
        PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_time||' skipped');

        CONTINUE;
    END;

    -- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_timestamp_min IS NOT NULL THEN
        IF v_time < v_sub_timestamp_min OR v_time > v_sub_timestamp_max THEN
            CONTINUE;
        END IF;
    END IF;

    -- This suffix generation code is in partition_data_time() as well
    v_partition_suffix := to_char(v_time, v_datetime_string);
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);
    -- Check if child exists. 
    SELECT count(*) INTO v_exists
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_parent_schema::name 
    AND c.relname = v_partition_name::name;

    IF v_exists > 0 THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Creating new partition %s.%s with interval from %s to %s'
                                                , v_parent_schema
                                                , v_partition_name
                                                , v_partition_timestamp_start
                                                , v_partition_timestamp_end-'1sec'::interval));
    END IF;

    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    -- Close parentheses on LIKE are below due to differing requirements of native subpartitioning
    -- Same INCLUDING list is used in create_parent()
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS '
                                , v_parent_schema
                                , v_partition_name
                                , v_parent_schema
                                , v_parent_tablename);

    SELECT sub_partition_type, sub_control INTO v_sub_partition_type, v_sub_control 
    FROM @extschema@.part_config_sub 
    WHERE sub_parent = p_parent_table;
    IF v_sub_partition_type = 'native' THEN
        -- NOTE: Need to handle this differently when index inheritance is supported natively
        -- Cannot include indexes since they cannot exist on native parents
        v_sql := v_sql || format(') PARTITION BY RANGE (%I) ', v_sub_control);
    ELSE
        v_sql := v_sql || format(' INCLUDING INDEXES) ', v_sub_control);
    END IF;

    SELECT relhasoids INTO v_hasoids 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'create_partition_time v_sql: %', v_sql;
    END IF;
    EXECUTE v_sql;

    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
    END IF;

    IF v_partition_type = 'native' THEN

        IF v_template_table IS NOT NULL THEN
            PERFORM @extschema@.inherit_template_properties(p_parent_table, v_parent_schema, v_partition_name);
        END IF;

        IF v_epoch = 'none' THEN
            -- Attach with normal, time-based values for native constraint
            EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                , v_parent_schema
                , v_parent_tablename
                , v_parent_schema
                , v_partition_name
                , v_partition_timestamp_start
                , v_partition_timestamp_end);
        ELSE
            -- Must attach with integer based values for native constraint and epoch
            IF v_epoch = 'seconds' THEN
                EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_parent_schema
                    , v_partition_name
                    , EXTRACT('epoch' FROM v_partition_timestamp_start)
                    , EXTRACT('epoch' FROM v_partition_timestamp_end));
            ELSIF v_epoch = 'milliseconds' THEN
                EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_parent_schema
                    , v_partition_name
                    , EXTRACT('epoch' FROM v_partition_timestamp_start) * 1000
                    , EXTRACT('epoch' FROM v_partition_timestamp_end) * 1000);
            END IF;
            -- Create secondary, time-based constraint since native's constraint is already integer based
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%s >= %L AND %4$s < %6$L)'
                , v_parent_schema
                , v_partition_name
                , v_partition_name||'_partition_check'
                , v_partition_expression
                , v_partition_timestamp_start
                , v_partition_timestamp_end);
        END IF;
    ELSE
        -- Non-native always gets time-based constraint
        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%s >= %L AND %4$s < %6$L)'
            , v_parent_schema
            , v_partition_name
            , v_partition_name||'_partition_check'
            , v_partition_expression
            , v_partition_timestamp_start
            , v_partition_timestamp_end);
        IF v_epoch = 'seconds' THEN
            -- Non-native needs secondary, integer based constraint for epoch
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                            , v_parent_schema
                            , v_partition_name
                            , v_partition_name||'_partition_int_check'
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_start)
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_end) );
        ELSIF v_epoch = 'milliseconds' THEN
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                            , v_parent_schema
                            , v_partition_name
                            , v_partition_name||'_partition_int_check'
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_start) * 1000
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_end) * 1000);
        END IF;

        EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I'
                        , v_parent_schema
                        , v_partition_name
                        , v_parent_schema
                        , v_parent_tablename);

        -- If custom time, set extra config options.
        IF v_partition_type = 'time-custom' THEN
            INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
            VALUES ( p_parent_table, v_parent_schema||'.'||v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
        END IF;

        -- Indexes cannot be created on the parent, so clustering cannot be used for native yet.
        PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

        -- Foreign keys to other tables not supported in native
        IF v_inherit_fk THEN
            PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
        END IF;

    END IF; -- end native check

    -- NOTE: Privileges currently not automatically inherited for native
    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_automatic_maintenance
            , sub_infinite_time_partitions
            , sub_jobmon
            , sub_trigger_exception_handling
            , sub_template_table
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Subpartitioning %s.%s', v_parent_schema, v_partition_name));
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_automatic_maintenance := %L
                , p_inherit_fk := %L
                , p_epoch := %L
                , p_template_table := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_automatic_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_template_table
            , v_row.sub_jobmon);
        IF p_debug THEN
            RAISE NOTICE 'create_partition_time (create_parent loop): %', v_sql;
        END IF;
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
            , trigger_exception_handling = v_row.sub_trigger_exception_handling
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

    END LOOP; -- end sub partitioning LOOP

    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    IF v_publications IS NOT NULL THEN
        -- NOTE: Publications currently not supported on parent table, but are supported on the table partitions if individually assigned.
        PERFORM @extschema@.apply_publications(p_parent_table, v_parent_schema, v_partition_name);
    END IF;

    v_partition_created := true;

END LOOP;
-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s. Attempted intervals: %s', p_parent_table, p_partition_times));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_sub_parent(
    p_top_parent text
    , p_control text
    , p_type text
    , p_interval text
    , p_native_check text DEFAULT NULL
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch text DEFAULT 'none' 
    , p_upsert text DEFAULT ''
    , p_trigger_return_null boolean DEFAULT true
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_child_interval        interval;
v_child_start_id        bigint;
v_child_start_time      timestamptz;
v_control               text;
v_control_parent_type   text;
v_control_sub_type      text;
v_last_partition        text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_epoch          text;
v_parent_interval       text;
v_parent_relkind        char;
v_parent_schema         text;
v_parent_tablename      text;
v_parent_type           text;
v_part_col              text;
v_partition_id_array    bigint[];
v_partition_time_array  timestamptz[];
v_relkind               char;
v_recreate_child        boolean := false;
v_row                   record;
v_row_last_part         record;
v_run_maint             boolean;
v_sql                   text;
v_success               boolean := false;
v_template_table        text;
v_top_type              text;

BEGIN
/*
 * Create a partition set that is a subpartition of an already existing partition set.
 * Given the parent table of any current partition set, it will turn all existing children into parent tables of their own partition sets
 *      using the configuration options given as parameters to this function.
 * Uses another config table that allows for turning all future child partitions into a new parent automatically.
 */

SELECT n.nspname, c.relname, c.relkind INTO v_parent_schema, v_parent_tablename, v_parent_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_top_parent, '.', 1)::name
AND c.relname = split_part(p_top_parent, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Please create parent table first: %', p_top_parent;
    END IF;

IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type', p_type;
END IF;

IF v_parent_relkind = 'p' AND p_type <> 'native' THEN
    RAISE EXCEPTION 'Cannot create a non-native sub-partition of a native parent table. All levels of a sub-partition set must be either all native or all non-native';
END IF;
 
SELECT partition_type, partition_interval, control, automatic_maintenance, epoch, template_table
INTO v_parent_type, v_parent_interval, v_control, v_run_maint, v_parent_epoch, v_template_table
FROM @extschema@.part_config 
WHERE parent_table = p_top_parent;
IF v_parent_type IS NULL THEN
    RAISE EXCEPTION 'Cannot subpartition a table that is not managed by pg_partman already. Given top parent table not found in @extschema@.part_config: %', p_top_parent;
END IF;

IF p_type = 'native' AND (lower(p_native_check) <> 'yes' OR p_native_check IS NULL) THEN
    RAISE EXCEPTION 'The sub-partitioning of a natively partitoned table is a DESTRUCTIVE process unless all child tables are already natively subpartitioned. All child tables, and therefore ALL DATA, may be destroyed since the parent table must be declared as partitioned on first creation and cannot be altered later. See docs for more info. Set p_native_check parameter to "yes" if you are sure this is ok.';
END IF;

IF p_upsert <> '' THEN
    IF current_setting('server_version_num')::int < 90500 THEN
        RAISE EXCEPTION 'INSERT ... ON CONFLICT (UPSERT) feature is only supported in PostgreSQL 9.5 and later';
    END IF;
    IF p_type = 'native' THEN
        RAISE EXCEPTION 'Native partitioning does not currently support upsert. Use pg_partman''s partitioning methods instead if this is required';
    END IF;
END IF;

SELECT general_type INTO v_control_parent_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Add the given parameters to the part_config_sub table first in case create_partition_* functions are called below 
-- All sub-partition parents must use the same template table for native partitioning, so ensure the one from the given parent is obtained and used.
INSERT INTO @extschema@.part_config_sub (
    sub_parent
    , sub_control
    , sub_partition_type
    , sub_partition_interval
    , sub_constraint_cols
    , sub_premake
    , sub_inherit_fk
    , sub_automatic_maintenance
    , sub_epoch
    , sub_upsert
    , sub_jobmon
    , sub_trigger_return_null
    , sub_template_table)
VALUES (
    p_top_parent
    , p_control
    , p_type
    , p_interval
    , p_constraint_cols
    , p_premake
    , p_inherit_fk
    , 'on' 
    , p_epoch
    , p_upsert
    , p_jobmon
    , p_trigger_return_null
    , v_template_table);

FOR v_row IN 
    -- Loop through all current children to turn them into partitioned tables
    SELECT partition_schemaname AS child_schema, partition_tablename AS child_tablename FROM @extschema@.show_partitions(p_top_parent)
LOOP

    SELECT general_type INTO v_control_sub_type FROM @extschema@.check_control_type(v_row.child_schema, v_row.child_tablename, p_control);

    SELECT c.relkind INTO v_relkind
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_row.child_schema
    AND c.relname = v_row.child_tablename;

    -- If both parent and sub-parent are the same partition type (time/id), ensure boundaries of sub-parent are within parent
    IF (v_control_parent_type = 'time' AND v_control_sub_type = 'time') OR
       (v_control_parent_type = 'id' AND v_parent_epoch <> 'none' AND v_control_sub_type = 'id' AND p_epoch <> 'none') THEN
        CASE
            WHEN p_interval = 'yearly' THEN
                v_child_interval := '1 year';
            WHEN p_interval = 'quarterly' THEN
                v_child_interval := '3 months';
            WHEN p_interval = 'monthly' THEN
                v_child_interval := '1 month';
            WHEN p_interval  = 'weekly' THEN
                v_child_interval := '1 week';
            WHEN p_interval = 'daily' THEN
                v_child_interval := '1 day';
            WHEN p_interval = 'hourly' THEN
                v_child_interval := '1 hour';
            WHEN p_interval = 'half-hour' THEN
                v_child_interval := '30 mins';
            WHEN p_interval = 'quarter-hour' THEN
                v_child_interval := '15 mins';
            ELSE
                v_child_interval := p_interval::interval;
                IF v_child_interval < '1 second'::interval THEN
                    RAISE EXCEPTION 'Partitioning interval must be 1 second or greater';
                END IF;
        END CASE;

        IF v_child_interval >= v_parent_interval::interval THEN
            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
            RAISE EXCEPTION 'Sub-partition interval cannot be greater than or equal to the given parent interval';
        END IF;
        IF v_child_interval = '1 week' AND v_parent_interval::interval > '1 week'::interval THEN
            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
            RAISE EXCEPTION 'Due to conflicting data boundaries between ISO weeks and any larger interval of time, pg_partman cannot support a sub-partition interval of weekly';
        END IF;

    ELSIF v_control_parent_type = 'id' AND v_control_sub_type = 'id' THEN
        IF p_interval::bigint >= v_parent_interval::bigint THEN
            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
            RAISE EXCEPTION 'Sub-partition interval cannot be greater than or equal to the given parent interval';
        END IF;
    END IF;
      
    IF p_type = 'native' THEN
        IF v_relkind <> 'p' THEN 
            -- Not natively partitioned already. Drop it and recreate as such.
            RAISE WARNING 'Child table % is not natively partitioned. Dropping and recreating with native partitioning'
                            , v_row.child_schema||'.'||v_row.child_tablename;
            SELECT child_start_time, child_start_id INTO v_child_start_time, v_child_start_id
            FROM @extschema@.show_partition_info(v_row.child_schema||'.'||v_row.child_tablename
                                                    , v_parent_interval
                                                    , p_top_parent);
            EXECUTE format('DROP TABLE %I.%I', v_row.child_schema, v_row.child_tablename);
            v_recreate_child := true;

            IF v_child_start_id IS NOT NULL THEN
                v_partition_id_array[0] := v_child_start_id;
                PERFORM @extschema@.create_partition_id(p_top_parent, v_partition_id_array, true);
            ELSIF v_child_start_time IS NOT NULL THEN
                v_partition_time_array[0] := v_child_start_time;
                PERFORM @extschema@.create_partition_time(p_top_parent, v_partition_time_array, true);
            END IF;
        ELSE
            SELECT a.attname
            INTO v_part_col
            FROM pg_attribute a
            JOIN pg_class c ON a.attrelid = c.oid
            JOIN pg_namespace n ON c.relnamespace = n.oid
            WHERE n.nspname = v_row.child_schema::name
            AND c.relname = v_row.child_tablename::name
            AND attnum IN (SELECT unnest(partattrs) FROM pg_partitioned_table p WHERE a.attrelid = p.partrelid);

            IF p_control <> v_part_col THEN
                RAISE EXCEPTION 'Attempted to natively sub-partition an existing table that has the partition column (%) defined differently than the control column given (%)', v_part_col, p_control;
            ELSE -- Child table is already natively subpartitioned properly. Skip the rest.
                CONTINUE;
            END IF;
        END IF; -- end 'p' relkind check

    END IF; -- end native check

    IF v_recreate_child = false THEN
    -- Always call create_parent() if child table wasn't recreated above.
    -- If it was, the create_partition_*() functions called above also call create_parent if any of the tables
    --  it creates are in the part_config_sub table. Since it was inserted there above,
    --  it should call it appropriately
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_automatic_maintenance := %L
                , p_start_partition := %L
                , p_inherit_fk := %L
                , p_epoch := %L
                , p_upsert := %L
                , p_trigger_return_null := %L
                , p_template_table := %L
                , p_jobmon := %L
                , p_debug := %L )'
            , v_row.child_schema||'.'||v_row.child_tablename
            , p_control
            , p_type
            , p_interval
            , p_constraint_cols
            , p_premake
            , 'on'
            , p_start_partition
            , p_inherit_fk
            , p_epoch
            , p_upsert
            , p_trigger_return_null
            , v_template_table
            , p_jobmon
            , p_debug);
        EXECUTE v_sql;
    END IF; -- end recreate check

END LOOP;

v_success := true;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_success;

END
$$;



CREATE OR REPLACE FUNCTION @extschema@.create_trigger(p_parent_table text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_function_name         text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_relkind               char;
v_trig_name             text;
v_trig_sql              text;

BEGIN
/*
 * Function to create partitioning trigger on parent table
 */

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

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

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

END
$$;



CREATE OR REPLACE FUNCTION @extschema@.drop_partition_column(p_parent_table text, p_column text) RETURNS void
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



CREATE OR REPLACE FUNCTION @extschema@.drop_partition_id(p_parent_table text, p_retention bigint DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                  text;
ex_detail                   text;
ex_hint                     text;
ex_message                  text;
v_adv_lock                  boolean;
v_control                   text;
v_control_type              text;
v_count                     int;
v_drop_count                int := 0;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_max                       bigint;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        bigint;
v_partition_id              bigint;
v_partition_type            text;
v_retention                 bigint;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_row                       record;
v_row_max_id                record;
v_step_id                   bigint;

BEGIN
/*
 * Function to drop child tables from an id-based partition set. 
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman drop_partition_id'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_id already running.';
    RETURN 0;
END IF;

IF p_retention IS NULL THEN
    SELECT  
        partition_interval::bigint
        , partition_type
        , control
        , retention::bigint
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
    INTO
        v_partition_interval
        , v_partition_type
        , v_control
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND retention IS NOT NULL;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE -- Allow override of configuration options
     SELECT  
        partition_interval::bigint
        , partition_type
        , control
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
    INTO
        v_partition_interval
        , v_partition_type
        , v_control
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table;
    v_retention := p_retention;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'id' THEN
    RAISE EXCEPTION 'Data type of control column in given partition set is not an integer type';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

-- Loop through child tables starting from highest to get current max value in partition set
-- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
FOR v_row_max_id IN
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
LOOP
        EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max_id.partition_schemaname, v_row_max_id.partition_tablename) INTO v_max;
        IF v_max IS NOT NULL THEN
            EXIT;
        END IF;
END LOOP;

-- Loop through child tables of the given parent
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC')
LOOP
     SELECT child_start_id INTO v_partition_id FROM @extschema@.show_partition_info(v_row.partition_schemaname||'.'||v_row.partition_tablename
        , v_partition_interval::text
        , p_parent_table);

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention <= (v_max - (v_partition_id + v_partition_interval)) THEN

        -- Do not allow final partition to be dropped
        SELECT count(*) INTO v_count FROM @extschema@.show_partitions(p_parent_table);
        IF v_count = 1 THEN
            RAISE WARNING 'Attempt to drop final partition in partition set % as part of retention policy. Advise reviewing retention policy and/or data entry into the partition set.', p_parent_table;
            CONTINUE;
        END IF;

        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP ID PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Detach/Uninherit table %s.%s from %s', v_row.partition_schemaname, v_row.partition_tablename, p_parent_table));
        END IF;
        IF v_partition_type = 'native' THEN
            EXECUTE format('ALTER TABLE %I.%I DETACH PARTITION %I.%I'
                , v_parent_schema
                , v_parent_tablename
                , v_row.partition_schemaname
                , v_row.partition_tablename);
        ELSE
            EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                , v_row.partition_schemaname
                , v_row.partition_tablename
                , v_parent_schema
                , v_parent_tablename);
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF;
        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                EXECUTE format('DROP TABLE %I.%I CASCADE', v_row.partition_schemaname, v_row.partition_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                FOR v_index IN 
                     WITH child_info AS (
                        SELECT c1.oid
                        FROM pg_catalog.pg_class c1
                        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                        WHERE c1.relname = v_row.partition_tablename::name
                        AND n1.nspname = v_row.partition_schema::name
                    )
                    SELECT c.relname as name
                        , con.conname
                    FROM pg_catalog.pg_index i
                    JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
                    LEFT JOIN pg_catalog.pg_constraint con ON i.indexrelid = con.conindid
                    JOIN child_info ON i.indrelid = child_info.oid
                LOOP
                    IF v_jobmon_schema IS NOT NULL THEN
                        v_step_id := add_step(v_job_id, format('Drop index %s from %s.%s'
                            , v_index.name
                            , v_row.partition_schemaname
                            , v_row.partition_tablename));
                    END IF;
                    IF v_index.conname IS NOT NULL THEN
                        EXECUTE format('ALTER TABLE %I.%I DROP CONSTRAINT %I', v_row.partition_schemaname, v_row.partition_tablename, v_index.conname);
                    ELSE
                        EXECUTE format('DROP INDEX %I.%I', v_row.partition_schemaname, v_index.name);
                    END IF;
                    IF v_jobmon_schema IS NOT NULL THEN
                        PERFORM update_step(v_step_id, 'OK', 'Done');
                    END IF;
                END LOOP;
            END IF;
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, format('Moving table %s.%s to schema %s'
                                                        , v_row.partition_schemaname
                                                        , v_row.partition_tablename
                                                        , v_retention_schema));
            END IF;

            EXECUTE format('ALTER TABLE %I.%I SET SCHEMA %I'
                    , v_row.partition_schemaname
                    , v_row.partition_tablename
                    , v_retention_schema);

            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_row.partition_schemaname ||'.'||v_row.partition_tablename;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    IF v_job_id IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
        PERFORM update_step(v_step_id, 'OK', format('%s partitions dropped.', v_drop_count));
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN DROP ID PARTITION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.drop_partition_time(p_parent_table text, p_retention interval DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                  text;
ex_detail                   text;
ex_hint                     text;
ex_message                  text;
v_adv_lock                  boolean;
v_control                   text;
v_control_type              text;
v_count                     int;
v_datetime_string           text;
v_drop_count                int := 0;
v_epoch                     text;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        interval;
v_partition_timestamp       timestamptz;
v_partition_type            text;
v_retention                 interval;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_row                       record;
v_step_id                   bigint;

BEGIN
/*
 * Function to drop child tables from a time-based partition set.
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman drop_partition_time'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_time already running.';
    RETURN 0;
END IF;

-- Allow override of configuration options
IF p_retention IS NULL THEN
    SELECT  
        partition_type
        , control
        , partition_interval::interval
        , epoch
        , retention::interval
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
        , jobmon
    INTO
        v_partition_type
        , v_control
        , v_partition_interval
        , v_epoch
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND retention IS NOT NULL;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
    SELECT  
        partition_type
        , partition_interval::interval
        , epoch
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
        , jobmon
    INTO
        v_partition_type
        , v_partition_interval
        , v_epoch
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table;
    v_retention := p_retention;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'time' THEN 
    IF (v_control_type = 'id' AND v_epoch = 'none') OR v_control_type <> 'id' THEN
        RAISE EXCEPTION 'Cannot run on partition set without time based control column or epoch flag set with an id column. Found control: %, epoch: %', v_control_type, v_epoch;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

-- Loop through child tables of the given parent
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
LOOP
    -- pull out datetime portion of partition's tablename to make the next one
     SELECT child_start_time INTO v_partition_timestamp FROM @extschema@.show_partition_info(v_row.partition_schemaname||'.'||v_row.partition_tablename
        , v_partition_interval::text
        , p_parent_table);
    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention < (CURRENT_TIMESTAMP - (v_partition_timestamp + v_partition_interval)) THEN

        -- Do not allow final partition to be dropped
        SELECT count(*) INTO v_count FROM @extschema@.show_partitions(p_parent_table);
        IF v_count = 1 THEN
            RAISE WARNING 'Attempt to drop final partition in partition set % as part of retention policy. If you see this message multiple times for the same table, advise reviewing retention policy and/or data entry into the partition set. Also consider setting "infinite_time_partitions = true" if there are large gaps in data insertion.).', p_parent_table;
            CONTINUE;
        END IF;

        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP TIME PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Detach/Uninherit table %s.%s from %s'
                                                , v_row.partition_schemaname
                                                , v_row.partition_tablename
                                                , p_parent_table));
        END IF;
        IF v_partition_type = 'native' THEN
            EXECUTE format('ALTER TABLE %I.%I DETACH PARTITION %I.%I'
                , v_parent_schema
                , v_parent_tablename
                , v_row.partition_schemaname
                , v_row.partition_tablename);
        ELSE
            EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                    , v_row.partition_schemaname
                    , v_row.partition_tablename
                    , v_parent_schema
                    , v_parent_tablename);
        END IF;
        IF v_partition_type = 'time-custom' THEN
            DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_row.partition_schemaname||'.'||v_row.partition_tablename;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                EXECUTE format('DROP TABLE %I.%I CASCADE', v_row.partition_schemaname, v_row.partition_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                FOR v_index IN 
                    WITH child_info AS (
                        SELECT c1.oid
                        FROM pg_catalog.pg_class c1
                        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                        WHERE c1.relname = v_row.partition_tablename::name
                        AND n1.nspname = v_row.partition_schemaname::name
                    )
                    SELECT c.relname as name
                        , con.conname
                    FROM pg_catalog.pg_index i
                    JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
                    LEFT JOIN pg_catalog.pg_constraint con ON i.indexrelid = con.conindid
                    JOIN child_info ON i.indrelid = child_info.oid
                LOOP
                    IF v_jobmon_schema IS NOT NULL THEN
                        v_step_id := add_step(v_job_id, format('Drop index %s from %s.%s'
                                                            , v_index.name
                                                            , v_row.partition_schemaname
                                                            , v_row.partition_tablename));
                    END IF;
                    IF v_index.conname IS NOT NULL THEN
                        EXECUTE format('ALTER TABLE %I.%I DROP CONSTRAINT %I'
                                        , v_row.partition_schemaname
                                        , v_row.partition_tablename
                                        , v_index.conname);
                    ELSE
                        EXECUTE format('DROP INDEX %I.%I', v_parent_schema, v_index.name);
                    END IF;
                    IF v_jobmon_schema IS NOT NULL THEN
                        PERFORM update_step(v_step_id, 'OK', 'Done');
                    END IF;
                END LOOP;
            END IF;
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, format('Moving table %s.%s to schema %s'
                                                , v_row.partition_schemaname
                                                , v_row.partition_tablename
                                                , v_retention_schema));
            END IF;

            EXECUTE format('ALTER TABLE %I.%I SET SCHEMA %I', v_row.partition_schemaname, v_row.partition_tablename, v_retention_schema);


            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_row.partition_schemaname||'.'||v_row.partition_tablename;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    IF v_job_id IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
        PERFORM update_step(v_step_id, 'OK', format('%s partitions dropped.', v_drop_count));
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN DROP TIME PARTITION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.inherit_template_properties (p_parent_table text, p_child_schema text, p_child_tablename text) RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_child_relkind         char;
v_child_schema          text;
v_child_tablename       text;
v_dupe_found            boolean := false;
v_fk_list               record;
v_index_list            record;
v_inherit_fk            boolean;
v_parent_index_list     record;
v_parent_oid            oid;
v_parent_table          text;
v_sql                   text;
v_template_oid          oid;
v_template_table        text;
v_template_tablespace   text;

BEGIN
/*
 * Function to inherit the properties of the template table to newly created child tables.
 * Currently used for PostgreSQL 10 to inherit indexes and FKs since that is not natively available
 * For PG11, used to inherit non-partition-key unique indexes & primary keys
 */

SELECT parent_table, template_table, inherit_fk
INTO v_parent_table, v_template_table, v_inherit_fk
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;
IF v_parent_table IS NULL THEN
    RAISE EXCEPTION 'Given parent table has no configuration in pg_partman: %', p_parent_table;
ELSIF v_template_table IS NULL THEN
    RAISE EXCEPTION 'No template table set in configuration for given parent table: %', p_parent_table;
END IF;
 
SELECT c.oid INTO v_parent_oid
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_oid IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs: %', p_parent_table;
    END IF;
 
SELECT n.nspname, c.relname, c.relkind INTO v_child_schema, v_child_tablename, v_child_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = p_child_schema::name
AND c.relname = p_child_tablename::name;
    IF v_child_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given child table in system catalogs: %.%', v_child_schema, v_child_tablename;
    END IF;
       
IF v_child_relkind = 'p' THEN
    -- Subpartitioned parent, do not apply properties
    RAISE DEBUG 'inherit_template_properties: found given child is subpartition parent, so properties not inherited';
    RETURN false;
END IF;

SELECT c.oid, ts.spcname INTO v_template_oid, v_template_tablespace
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace ts ON c.reltablespace = ts.oid
WHERE n.nspname = split_part(v_template_table, '.', 1)::name
AND c.relname = split_part(v_template_table, '.', 2)::name;
    IF v_template_oid IS NULL THEN
        RAISE EXCEPTION 'Unable to find configured template table in system catalogs: %', v_template_table;
    END IF;

-- Index creation (Required for all indexes in PG10. Only for non-unique, non-partition key indexes in PG11)
IF current_setting('server_version_num')::int >= 100000 THEN
    FOR v_index_list IN 
        SELECT
        array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
        , i.indisprimary
        , i.indisunique
        , ( SELECT array_agg( a.attname ORDER by x.r )
            FROM pg_catalog.pg_attribute a
            JOIN ( SELECT k, row_number() over () as r
                    FROM unnest(i.indkey) k ) as x
            ON a.attnum = x.k AND a.attrelid = i.indrelid
        ) AS indkey_names
        , c.relname AS index_name
        , ts.spcname AS tablespace_name
        FROM pg_catalog.pg_index i
        JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
        LEFT OUTER JOIN pg_catalog.pg_tablespace ts ON c.reltablespace = ts.oid
        WHERE i.indrelid = v_template_oid
        AND i.indisvalid
        ORDER BY 1
    LOOP
        IF current_setting('server_version_num')::int >= 110000 THEN
            FOR v_parent_index_list IN 
                SELECT
                array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
                , i.indisprimary
                , ( SELECT array_agg( a.attname ORDER by x.r )
                    FROM pg_catalog.pg_attribute a
                    JOIN ( SELECT k, row_number() over () as r
                            FROM unnest(i.indkey) k ) as x
                    ON a.attnum = x.k AND a.attrelid = i.indrelid
                ) AS indkey_names
                FROM pg_catalog.pg_index i
                WHERE i.indrelid = v_parent_oid
                AND i.indisvalid
                ORDER BY 1
            LOOP

                IF v_parent_index_list.indisprimary AND v_index_list.indisprimary THEN
                    IF v_parent_index_list.indkey_names = v_index_list.indkey_names THEN
                        RAISE DEBUG 'Ignoring duplicate primary key on template table: % ', v_index_list.indkey_names;
                        v_dupe_found := true;
                        CONTINUE; -- only continue within this nested loop
                    END IF;
                END IF;

                IF v_parent_index_list.statement = v_index_list.statement THEN
                    RAISE DEBUG 'Ignoring duplicate index on template table: %', v_index_list.statement;
                    v_dupe_found := true;
                    CONTINUE; -- only continue within this nested loop
                END IF;

            END LOOP; -- end parent index loop
        END IF; -- End PG11 check

        IF v_dupe_found = true THEN
            -- Only used in PG11 and should skip trying to create indexes that already existed on the parent
            CONTINUE;
        END IF;

        IF v_index_list.indisprimary THEN
            v_sql := format('ALTER TABLE %I.%I ADD PRIMARY KEY (%s)'
                            , v_child_schema
                            , v_child_tablename
                            , '"' || array_to_string(v_index_list.indkey_names, '","') || '"');
            IF v_index_list.tablespace_name IS NOT NULL THEN
                v_sql := v_sql || format(' USING INDEX TABLESPACE %I', v_index_list.tablespace_name);
            END IF;
            RAISE DEBUG 'Create pk: %', v_sql;
            EXECUTE v_sql;
        ELSE
            -- statement column should be just the portion of the index definition that defines what it actually is
            v_sql := format('CREATE %s INDEX ON %I.%I %s', CASE WHEN v_index_list.indisunique = TRUE THEN 'UNIQUE' ELSE '' END, v_child_schema, v_child_tablename, v_index_list.statement);
            IF v_index_list.tablespace_name IS NOT NULL THEN
                v_sql := v_sql || format(' TABLESPACE %I', v_index_list.tablespace_name);
            END IF;

            RAISE DEBUG 'Create index: %', v_sql;
            EXECUTE v_sql;

        END IF;

    END LOOP;
END IF; 
-- End index creation

-- Foreign key creation (PG10 only)
IF current_setting('server_version_num')::int >= 100000 AND current_setting('server_version_num')::int < 110000 THEN
    IF v_inherit_fk THEN
        FOR v_fk_list IN 
            SELECT pg_get_constraintdef(con.oid) AS constraint_def
            FROM pg_catalog.pg_constraint con
            JOIN pg_catalog.pg_class c ON con.conrelid = c.oid
            WHERE c.oid = v_template_oid
            AND contype = 'f'
        LOOP
            v_sql := format('ALTER TABLE %I.%I ADD %s', v_child_schema, v_child_tablename, v_fk_list.constraint_def);
            RAISE DEBUG 'Create FK: %', v_sql;
            EXECUTE v_sql;
        END LOOP;
    END IF;
END IF;
-- End foreign key creation

-- Tablespace inheritance
IF v_template_tablespace IS NOT NULL THEN
    v_sql := format('ALTER TABLE %I.%I SET TABLESPACE %I', v_child_schema, v_child_tablename, v_template_tablespace);
    EXECUTE v_sql;
    RAISE DEBUG 'Alter tablespace: %', v_sql;
END IF;

RETURN true;

END
$$;


CREATE FUNCTION @extschema@.partition_data_id(p_parent_table text
        , p_batch_count int DEFAULT 1
        , p_batch_interval bigint DEFAULT NULL
        , p_lock_wait numeric DEFAULT 0
        , p_order text DEFAULT 'ASC'
        , p_analyze boolean DEFAULT true
        , p_source_table text DEFAULT NULL) 
    RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE

v_control                   text;
v_control_type              text;
v_current_partition_name    text;
v_epoch                     text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_id          bigint;
v_min_partition_id          bigint;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_parent_tablename_real     text;
v_partition_interval        bigint;
v_partition_id              bigint[];
v_partition_type            text;
v_rowcount                  bigint;
v_sql                       text;
v_start_control             bigint;
v_total_rows                bigint := 0;

BEGIN
/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */

SELECT partition_interval::bigint
    , partition_type
    , control
    , epoch
INTO v_partition_interval
    , v_partition_type
    , v_control
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for given table:  %', p_parent_table;
END IF;

IF v_partition_type = 'native' AND p_source_table IS NULL THEN
    RAISE EXCEPTION 'Partitioning data for a native partition set requires the p_source_table parameter to be set.';
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

IF v_control_type <> 'id' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
    RAISE EXCEPTION 'Control column for given partition set is not id/serial based or epoch flag is set for time-based partitioning.';
END IF;

-- Replace the parent variables with the source variables if using source table for child table data
IF p_source_table IS NOT NULL THEN
    v_parent_tablename_real := v_parent_tablename; -- Preserve for use later
    v_parent_schema := NULL;
    v_parent_tablename := NULL;

    SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
    FROM pg_catalog.pg_tables
    WHERE schemaname = split_part(p_source_table, '.', 1)::name
    AND tablename = split_part(p_source_table, '.', 2)::name;

    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Given source table does not exist in system catalogs: %', p_source_table;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE format('SELECT min(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_partition_interval);
        v_partition_id := ARRAY[v_min_partition_id];
        -- Check if custom batch interval overflows current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_id + v_partition_interval) THEN
            v_max_partition_id := v_min_partition_id + v_partition_interval;
        ELSE
            v_max_partition_id := v_start_control + p_batch_interval;
        END IF;

    ELSIF p_order = 'DESC' THEN
        EXECUTE 'SELECT max('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_partition_interval);
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_id := v_min_partition_id + v_partition_interval;
        v_partition_id := ARRAY[v_min_partition_id];
        -- Make sure minimum doesn't underflow current partition minimum
        IF (v_start_control - p_batch_interval) >= v_min_partition_id THEN
            v_min_partition_id = v_start_control - p_batch_interval;
        END IF;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

    -- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                v_sql := format('SELECT * FROM ONLY %I.%I WHERE %I >= %s AND %I < %s FOR UPDATE NOWAIT'
                                , v_parent_schema
                                , v_parent_tablename
                                , v_control
                                , v_min_partition_id
                                , v_control
                                , v_max_partition_id);
                EXECUTE v_sql;
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
           RETURN -1;
        END IF;
    END IF;

    PERFORM @extschema@.create_partition_id(p_parent_table, v_partition_id, p_analyze);
    v_current_partition_name := @extschema@.check_name_length(COALESCE(v_parent_tablename_real, v_parent_tablename), v_min_partition_id::text, TRUE);

    EXECUTE format('WITH partition_data AS (
                        DELETE FROM ONLY %I.%I WHERE %I >= %s AND %I < %s RETURNING *)
                    INSERT INTO %I.%I SELECT * FROM partition_data'
                , v_parent_schema
                , v_parent_tablename
                , v_control
                , v_min_partition_id
                , v_control
                , v_max_partition_id
                , v_parent_schema
                , v_current_partition_name);

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP;

IF v_partition_type = 'partman' THEN
    PERFORM @extschema@.create_function_id(p_parent_table);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total_rows;

END
$$;


CREATE FUNCTION @extschema@.partition_data_time(
        p_parent_table text
        , p_batch_count int DEFAULT 1
        , p_batch_interval interval DEFAULT NULL
        , p_lock_wait numeric DEFAULT 0
        , p_order text DEFAULT 'ASC'
        , p_analyze boolean DEFAULT true
        , p_source_table text DEFAULT NULL)
    RETURNS bigint
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_control                   text;
v_control_type              text;
v_datetime_string           text;
v_current_partition_name    text;
v_epoch                     text;
v_last_partition            text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_timestamp   timestamptz;
v_min_partition_timestamp   timestamptz;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_parent_tablename_real     text;
v_partition_expression      text;
v_partition_interval        interval;
v_partition_suffix          text;
v_partition_timestamp       timestamptz[];
v_partition_type            text;
v_rowcount                  bigint;
v_start_control             timestamptz;
v_total_rows                bigint := 0;

BEGIN
/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */

SELECT partition_type
    , partition_interval::interval
    , control
    , datetime_string
    , epoch
INTO v_partition_type
    , v_partition_interval
    , v_control
    , v_datetime_string
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for given table:  %', p_parent_table;
END IF;

IF v_partition_type = 'native' AND p_source_table IS NULL THEN
    RAISE EXCEPTION 'Partitioning data for a native partition set requires the p_source_table parameter to be set.';
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'time' THEN 
    IF (v_control_type = 'id' AND v_epoch = 'none') OR v_control_type <> 'id' THEN
        RAISE EXCEPTION 'Cannot run on partition set without time based control column or epoch flag set with an id column. Found control: %, epoch: %', v_control_type, v_epoch;
    END IF;
END IF;

-- Replace the parent variables with the source variables if using source table for child table data
IF p_source_table IS NOT NULL THEN
    v_parent_tablename_real := v_parent_tablename; -- Preserve for use later
    v_parent_schema := NULL;
    v_parent_tablename := NULL;

    SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
    FROM pg_catalog.pg_tables
    WHERE schemaname = split_part(p_source_table, '.', 1)::name
    AND tablename = split_part(p_source_table, '.', 2)::name;

    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Given source table does not exist in system catalogs: %', p_source_table;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
    ELSE format('%I', v_control)
END;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE format('SELECT min(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_parent_tablename) INTO v_start_control;
    ELSIF p_order = 'DESC' THEN
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_parent_tablename) INTO v_start_control;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

    IF v_start_control IS NULL THEN
        EXIT;
    END IF;

    IF v_partition_type = 'partman' THEN
        CASE
            WHEN v_partition_interval = '15 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '15min'::interval * floor(date_part('minute', v_start_control) / 15.0);
            WHEN v_partition_interval = '30 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '30min'::interval * floor(date_part('minute', v_start_control) / 30.0);
            WHEN v_partition_interval = '1 hour' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control);
            WHEN v_partition_interval = '1 day' THEN
                v_min_partition_timestamp := date_trunc('day', v_start_control);
            WHEN v_partition_interval = '1 week' THEN
                v_min_partition_timestamp := date_trunc('week', v_start_control);
            WHEN v_partition_interval = '1 month' THEN
                v_min_partition_timestamp := date_trunc('month', v_start_control);
            WHEN v_partition_interval = '3 months' THEN
                v_min_partition_timestamp := date_trunc('quarter', v_start_control);
            WHEN v_partition_interval = '1 year' THEN
                v_min_partition_timestamp := date_trunc('year', v_start_control);
        END CASE;
    ELSIF v_partition_type IN ('time-custom', 'native') THEN
        SELECT child_start_time INTO v_min_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition
            , v_partition_interval::text
            , p_parent_table);
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        LOOP
            IF v_start_control >= v_min_partition_timestamp AND v_start_control < v_max_partition_timestamp THEN
                EXIT;
            ELSE
                BEGIN
                    IF v_start_control > v_max_partition_timestamp THEN
                        -- Keep going forward in time, checking if child partition time interval encompasses the current v_start_control value
                        v_min_partition_timestamp := v_max_partition_timestamp;
                        v_max_partition_timestamp := v_max_partition_timestamp + v_partition_interval;

                    ELSE
                        -- Keep going backwards in time, checking if child partition time interval encompasses the current v_start_control value
                        v_max_partition_timestamp := v_min_partition_timestamp;
                        v_min_partition_timestamp := v_min_partition_timestamp - v_partition_interval;
                    END IF;
                EXCEPTION WHEN datetime_field_overflow THEN
                    RAISE EXCEPTION 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                        Unable to create partition with interval before timestamp % ', v_min_partition_timestamp;
                END;
            END IF;
        END LOOP;

    END IF;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
    IF p_order = 'ASC' THEN
        -- Ensure batch interval given as parameter doesn't cause maximum to overflow the current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_timestamp + v_partition_interval) THEN
            v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        ELSE
            v_max_partition_timestamp := v_start_control + p_batch_interval;
        END IF;
    ELSIF p_order = 'DESC' THEN
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        -- Ensure batch interval given as parameter doesn't cause minimum to underflow current partition minimum
        IF (v_start_control - p_batch_interval) >= v_min_partition_timestamp THEN
            v_min_partition_timestamp = v_start_control - p_batch_interval;
        END IF;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

-- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                EXECUTE format('SELECT * FROM ONLY %I.%I WHERE %s >= %L AND %3$s < %5$L FOR UPDATE NOWAIT'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_partition_expression
                    , v_min_partition_timestamp
                    , v_max_partition_timestamp);
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
           RETURN -1;
        END IF;
    END IF;

    PERFORM @extschema@.create_partition_time(p_parent_table, v_partition_timestamp, p_analyze);
    -- This suffix generation code is in create_partition_time() as well
    v_partition_suffix := to_char(v_min_partition_timestamp, v_datetime_string);
    v_current_partition_name := @extschema@.check_name_length(COALESCE(v_parent_tablename_real, v_parent_tablename), v_partition_suffix, TRUE);

    EXECUTE format('WITH partition_data AS (
                        DELETE FROM ONLY %I.%I WHERE %s >= %L AND %3$s < %5$L RETURNING *)
                     INSERT INTO %I.%I SELECT * FROM partition_data'
                        , v_parent_schema
                        , v_parent_tablename
                        , v_partition_expression
                        , v_min_partition_timestamp
                        , v_max_partition_timestamp
                        , v_parent_schema
                        , v_current_partition_name);
    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

IF v_partition_type IN ('partman', 'time-custom') THEN
    PERFORM @extschema@.create_function_time(p_parent_table);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total_rows;

END
$$;



CREATE OR REPLACE FUNCTION @extschema@.reapply_privileges(p_parent_table text) RETURNS void
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context          text;
ex_detail           text;
ex_hint             text;
ex_message          text;
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_new_search_path   text := '@extschema@,pg_temp';
v_old_search_path   text;
v_parent_schema     text;
v_parent_tablename  text;
v_row               record;
v_step_id           bigint;

BEGIN

/*
 * Function to re-apply ownership & privileges on all child tables in a partition set using parent table as reference
 */

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_table;
IF v_jobmon IS NULL THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %', p_parent_table;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');


SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;
IF v_parent_tablename IS NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'Given parent table does not exist: %', p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s', p_parent_table));
END IF;

FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC')
LOOP
    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_row.partition_schemaname, v_row.partition_tablename, v_job_id);
END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.run_maintenance(p_parent_table text DEFAULT NULL, p_analyze boolean DEFAULT NULL, p_jobmon boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS void 
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_adv_lock                      boolean;
v_analyze                       boolean;
v_check_subpart                 int;
v_control_type                  text;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_id          bigint;
v_current_partition_timestamp   timestamptz;
v_default_tablename             text;
v_drop_count                    int := 0;
v_is_default                    text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_created        boolean;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamptz;
v_max_id_parent                 bigint;
v_max_time_parent               timestamptz;
v_new_search_path               text := '@extschema@,pg_temp';
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamptz;
v_old_search_path               text;
v_parent_exists                 text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_expression          text;
v_premade_count                 int;
v_premake_id_max                bigint;
v_premake_id_min                bigint;
v_premake_timestamp_min         timestamptz;
v_premake_timestamp_max         timestamptz;
v_row                           record;
v_row_max_id                    record;
v_row_max_time                  record;
v_row_sub                       record;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_id_max                    bigint;
v_sub_id_max_suffix             bigint;
v_sub_id_min                    bigint;
v_sub_parent                    text;
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_max_suffix      timestamptz;
v_sub_timestamp_min             timestamptz;
v_tablename                     text;
v_tables_list_sql               text;

BEGIN
/*
 * Function to manage pre-creation of the next partitions in a set.
 * Also manages dropping old partitions if the retention option is set.
 * If p_parent_table is passed, will only run run_maintenance() on that one table (no matter what the configuration table may have set for it)
 * Otherwise, will run on all tables in the config table with p_automatic_maintenance() set to true.
 * For large partition sets, running analyze can cause maintenance to take longer than expected. Can set p_analyze to false to avoid a forced analyze run on PG versions before 11. 11+ does not analyze by default anymore.
 * Be aware that constraint exclusion may not work properly until an analyze on the partition set is run. 
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

v_row := NULL; -- Ensure it's reset

v_tables_list_sql := 'SELECT parent_table
                , partition_type
                , partition_interval
                , control
                , premake
                , undo_in_progress
                , sub_partition_set_full
                , epoch
                , infinite_time_partitions
                , retention
            FROM @extschema@.part_config
            WHERE undo_in_progress = false';

IF p_parent_table IS NULL THEN
    v_tables_list_sql := v_tables_list_sql || ' AND automatic_maintenance = ''on''';
ELSE
    v_tables_list_sql := v_tables_list_sql || format(' AND parent_table = %L', p_parent_table);
END IF;

FOR v_row IN EXECUTE v_tables_list_sql
LOOP

    CONTINUE WHEN v_row.undo_in_progress;

    -- When sub-partitioning, retention may drop tables that were already put into the query loop values. 
    -- Check if they still exist in part_config before continuing
    v_parent_exists := NULL;
    SELECT parent_table INTO v_parent_exists FROM @extschema@.part_config WHERE parent_table = v_row.parent_table;
    RAISE DEBUG 'Parent table possibly removed from part_config by retenion';
    CONTINUE WHEN v_parent_exists IS NULL;
    
    -- Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
    -- Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
    -- This way at least provides a consistent way to check that I know will run. If anyone can get a working constraint/trigger, please help!
    SELECT sub_parent INTO v_sub_parent FROM @extschema@.part_config_sub WHERE sub_parent = v_row.parent_table;
    IF v_sub_parent IS NOT NULL THEN
        SELECT count(*) INTO v_check_subpart FROM @extschema@.check_subpart_sameconfig(v_row.parent_table);
        IF v_check_subpart > 1 THEN
            RAISE EXCEPTION 'Inconsistent data in part_config_sub table. Sub-partition tables that are themselves sub-partitions cannot have differing configuration values among their siblings. 
            Run this query: "SELECT * FROM @extschema@.check_subpart_sameconfig(''%'');" This should only return a single row or nothing. 
            If multiple rows are returned, results are all children of the given parent. Update the differing values to be consistent for your desired values.', v_row.sub_parent;
        END IF;
    END IF;

    -- Shouldn't need to analyze tables for most statistics for native sets on PG11+ by default anymore
    IF p_analyze IS NULL THEN
        IF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
            v_analyze := false;
        ELSE
            v_analyze := true;
        END IF;
    END IF;

    SELECT n.nspname, c.relname
    INTO v_parent_schema, v_parent_tablename 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(v_row.parent_table, '.', 1)::name
    AND c.relname = split_part(v_row.parent_table, '.', 2)::name;

    -- Used below to see if there's any data in the parent (<=PG10) or default (PG11+) child table.
    IF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
        -- Always returns the default partition first if it exists
        SELECT partition_tablename INTO v_default_tablename 
        FROM @extschema@.show_partitions(v_row.parent_table, p_include_default := true) LIMIT 1;

        SELECT pg_get_expr(relpartbound, v_row.parent_table::regclass) INTO v_is_default 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n on c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_default_tablename;

        IF v_is_default != 'DEFAULT' THEN
            v_default_tablename := v_parent_tablename;
        END IF;
    ELSE
        v_default_tablename := v_parent_tablename;
    END IF;

    SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_row.control);

    v_partition_expression := CASE
        WHEN v_row.epoch = 'seconds' THEN format('to_timestamp(%I)', v_row.control)
        WHEN v_row.epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_row.control)
        ELSE format('%I', v_row.control)
    END;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: v_partition_expression: %', v_partition_expression;
    END IF;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: parent_table: %, v_last_partition: %', v_row.parent_table, v_last_partition;
    END IF;

    IF v_control_type = 'time' OR (v_control_type = 'id' AND v_row.epoch <> 'none') THEN

        -- Run retention if needed
        IF v_row.retention IS NOT NULL THEN
            v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
            IF v_drop_count > 0 AND v_row.partition_type <> 'native' THEN
                PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
            END IF;
        END IF;

        IF v_row.sub_partition_set_full THEN CONTINUE; END IF;

        SELECT child_start_time INTO v_last_partition_timestamp 
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.

        IF v_row.infinite_time_partitions IS TRUE THEN
            -- Set it to "now" so new partitions continue to be created
            -- For infinite_time_partitions, don't bother getting the max value in the partitions
            v_current_partition_timestamp = CURRENT_TIMESTAMP;
        ELSE 
             FOR v_row_max_time IN
               SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
            LOOP
                EXECUTE format('SELECT max(%s)::text FROM %I.%I'
                                    , v_partition_expression
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;

                IF v_current_partition_timestamp IS NOT NULL THEN
                    SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_timestamp::text);
                    EXIT;
                END IF;
            END LOOP;
        END IF; -- end infinite time check

        -- Check for values in the parent/default table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_default_tablename) INTO v_max_time_parent;
        IF p_debug THEN
            RAISE NOTICE 'run_maint: v_current_partition_timestamp: %, v_max_time_parent: %', v_current_partition_timestamp, v_max_time_parent;
        END IF;
        IF v_max_time_parent > v_current_partition_timestamp THEN
            SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_max_time_parent::text);
        END IF;

        IF v_current_partition_timestamp IS NULL THEN 
            -- Partition set is completely empty and infinite time partitions not set
            -- Nothing to do
            CONTINUE;
        END IF;

        -- If this is a subpartition, determine if the last child table has been made. If so, mark it as full so future maintenance runs can skip it
        SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'time');
        IF v_sub_timestamp_max IS NOT NULL THEN
            SELECT suffix_timestamp INTO v_sub_timestamp_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_timestamp_max::text);
            IF v_sub_timestamp_max_suffix = v_last_partition_timestamp THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        -- Check and see how many premade partitions there are.
        v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        v_next_partition_timestamp := v_last_partition_timestamp;
        IF p_debug THEN
            RAISE NOTICE 'run_maint before loop: current_partition_timestamp: %, v_premade_count: %, v_sub_timestamp_min: %, v_sub_timestamp_max: %'
                , v_current_partition_timestamp 
                , v_premade_count
                , v_sub_timestamp_min
                , v_sub_timestamp_max;
        END IF;
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed
        WHILE (v_premade_count < v_row.premake) LOOP
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_timestamp: %', v_row.parent_table, v_premade_count, v_next_partition_timestamp;
            END IF;
            IF v_next_partition_timestamp < v_sub_timestamp_min OR v_next_partition_timestamp > v_sub_timestamp_max THEN
                -- With subpartitioning, no need to run if the timestamp is not in the parent table's range
                EXIT;
            END IF;
            BEGIN
                v_next_partition_timestamp := v_next_partition_timestamp + v_row.partition_interval::interval;
            EXCEPTION WHEN datetime_field_overflow THEN
                v_premade_count := v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                    PERFORM update_step(v_step_overflow_id, 'CRITICAL', format('Child partition creation skipped for parent table: %s', v_partition_time));
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;

            v_last_partition_created := @extschema@.create_partition_time(v_row.parent_table
                                                        , ARRAY[v_next_partition_timestamp]
                                                        , v_analyze
                                                        , p_debug := p_debug); 
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                IF v_row.partition_type <> 'native' THEN
                    PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
                END IF;
            END IF;

            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        END LOOP;

    ELSIF v_control_type = 'id' THEN

        -- Run retention if needed
        IF v_row.retention IS NOT NULL THEN
            v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);   
            IF v_drop_count > 0 AND v_row.partition_type <> 'native' THEN
                PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
            END IF;
        END IF;

        IF v_row.sub_partition_set_full THEN CONTINUE; END IF;

        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.

        FOR v_row_max_id IN
            SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            EXECUTE format('SELECT max(%I)::text FROM %I.%I'
                            , v_row.control
                            , v_row_max_id.partition_schemaname
                            , v_row_max_id.partition_tablename) INTO v_current_partition_id;
            IF v_current_partition_id IS NOT NULL THEN
                SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_id::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent/default table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_default_tablename) INTO v_max_id_parent;
        IF v_max_id_parent > v_current_partition_id THEN
            SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_max_id_parent::text);
        END IF;
        IF v_current_partition_id IS NULL THEN
            -- Partition set is completely empty. Nothing to do
            CONTINUE;
        END IF;

        SELECT child_start_id INTO v_last_partition_id
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Determine if this table is a child of a subpartition parent. If so, get limits to see if run_maintenance even needs to run for it.
        SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'id');
        IF v_sub_id_max IS NOT NULL THEN
            SELECT suffix_id INTO v_sub_id_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_id_max::text);
            IF v_sub_id_max_suffix = v_last_partition_id THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        v_next_partition_id := v_last_partition_id;
        v_premade_count := ((v_last_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
        WHILE (v_premade_count < v_row.premake) LOOP 
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_id: %', v_row.parent_table, v_premade_count, v_next_partition_id;
            END IF;
            IF v_next_partition_id < v_sub_id_min OR v_next_partition_id > v_sub_id_max THEN
                -- With subpartitioning, no need to run if the id is not in the parent table's range
                EXIT;
            END IF;
            v_next_partition_id := v_next_partition_id + v_row.partition_interval::bigint;
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], v_analyze);
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                IF v_row.partition_type <> 'native' THEN
                    PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
                END IF;
            END IF;
            v_premade_count := ((v_next_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        END LOOP;

    END IF; -- end main IF check for time or id

END LOOP; -- end of creation loop

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', format('Partition maintenance finished. %s partitons made. %s partitions dropped.', v_create_count, v_drop_count));
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN RUN MAINTENANCE'')', v_jobmon_schema) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



CREATE OR REPLACE FUNCTION @extschema@.show_partition_info(p_child_table text
    , p_partition_interval text DEFAULT NULL
    , p_parent_table text DEFAULT NULL
    , OUT child_start_time timestamptz
    , OUT child_end_time timestamptz
    , OUT child_start_id bigint
    , OUT child_end_id bigint 
    , OUT suffix text)
RETURNS record
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_child_schema          text;
v_child_tablename       text;
v_control               text;
v_control_type          text;
v_datetime_string       text;
v_epoch                 text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_table          text;
v_partition_interval    text;
v_partition_type        text;
v_quarter               text;
v_suffix                text;
v_suffix_position       int;
v_year                  text;

BEGIN
/*
 * Show the data boundries for a given child table as well as the suffix that will be used.
 * Passing the parent table argument improves performance by avoiding a catalog lookup.
 * Passing an interval lets you set one different than the default configured one if desired.
 */

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT n.nspname, c.relname INTO v_child_schema, v_child_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_child_table, '.', 1)::name
AND c.relname = split_part(p_child_table, '.', 2)::name;

IF v_child_tablename IS NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'Child table given does not exist (%)', p_child_table;
END IF;

IF p_parent_table IS NULL THEN
    SELECT n.nspname||'.'|| c.relname INTO v_parent_table
    FROM pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhparent
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhrelid::regclass = p_child_table::regclass;
ELSE
    v_parent_table := p_parent_table;
END IF;

IF p_partition_interval IS NULL THEN
    SELECT control, partition_interval, partition_type, datetime_string, epoch
    INTO v_control, v_partition_interval, v_partition_type, v_datetime_string, v_epoch
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
ELSE
    v_partition_interval := p_partition_interval;
    SELECT control, partition_type, datetime_string, epoch 
    INTO v_control, v_partition_type, v_datetime_string, v_epoch
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
END IF;

IF v_control IS NULL THEN
    RAISE EXCEPTION 'Parent table of given child not managed by pg_partman: %', v_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_child_schema, v_child_tablename, v_control);

v_suffix_position := (length(v_child_tablename) - position('p_' in reverse(v_child_tablename))) + 2;
v_suffix := substring(v_child_tablename from v_suffix_position);

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN

        IF v_partition_interval::interval <> '3 months' OR (v_partition_interval::interval = '3 months' AND v_partition_type = 'time-custom') THEN
           child_start_time := to_timestamp(v_suffix, v_datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(v_suffix, 'q', 1);
            v_quarter := split_part(v_suffix, 'q', 2);
            CASE
                WHEN v_quarter = '1' THEN
                    child_start_time := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
                WHEN v_quarter = '2' THEN
                    child_start_time := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
                WHEN v_quarter = '3' THEN
                    child_start_time := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
                WHEN v_quarter = '4' THEN
                    child_start_time := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            END CASE;
        END IF;

        child_end_time := (child_start_time + v_partition_interval::interval) - '1 second'::interval;

ELSIF v_control_type = 'id' THEN

    child_start_id := v_suffix::bigint;
    child_end_id := (child_start_id + v_partition_interval::bigint) - 1;

ELSE
    RAISE EXCEPTION 'Invalid partition type encountered in show_partition_info()';
END IF;

suffix = v_suffix;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN;

END
$$;



CREATE FUNCTION @extschema@.show_partitions (p_parent_table text, p_order text DEFAULT 'ASC', p_include_default boolean DEFAULT false) RETURNS TABLE (partition_schemaname text, partition_tablename text)
    LANGUAGE plpgsql STABLE
    SET search_path = @extschema@,pg_temp
    AS $$
DECLARE

v_control               text;
v_control_type          text;
v_datetime_string       text;
v_default_sql           text;
v_epoch                 text;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_interval    text;
v_partition_type        text;
v_sql                   text;

BEGIN
/*
 * Function to list all child partitions in a set in logical order.
 * Default partition is not listed by default since that's the common usage internally
 * If p_include_default is set true, default is always listed first.
 */

IF upper(p_order) NOT IN ('ASC', 'DESC') THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'p_order paramter must be one of the following values: ASC, DESC';
END IF;

SELECT partition_type
    , partition_interval
    , datetime_string
    , control
    , epoch
INTO v_partition_type
    , v_partition_interval
    , v_datetime_string
    , v_control
    , v_epoch
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF v_partition_type IS NULL THEN
    RAISE EXCEPTION 'Given parent table not managed by pg_partman: %', p_parent_table;
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_parent_tablename IS NULL THEN
    RAISE EXCEPTION 'Given parent table not found in system catalogs: %', p_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

RAISE DEBUG 'show_partitions: v_parent_schema: %, v_parent_tablename: %, v_datetime_string: %', v_parent_schema, v_parent_tablename, v_datetime_string;

v_sql := format('SELECT n.nspname::text AS partition_schemaname, c.relname::text AS partition_name FROM
        pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE h.inhparent = ''%I.%I''::regclass'
    , v_parent_schema
    , v_parent_tablename);

IF v_partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN

    IF p_include_default THEN
        -- Return the default partition immediately as first item in list
        v_default_sql := v_sql || format('
            AND pg_get_expr(relpartbound, c.oid) = ''DEFAULT''');
        RAISE DEBUG 'show_partitions: v_default_sql: %', v_default_sql;
        RETURN QUERY EXECUTE v_default_sql;
    END IF;

    v_sql := v_sql || format('
        AND pg_get_expr(relpartbound, c.oid) != ''DEFAULT''');

END IF;

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN

    IF v_partition_interval::interval <> '3 months' OR (v_partition_interval::interval = '3 months' AND v_partition_type = 'time-custom') THEN
        v_sql := v_sql || format('
            ORDER BY to_timestamp(substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) ), %L) %s'
            , v_datetime_string
            , p_order);

    ELSE

        -- For quarterly, to_timestamp() doesn't recognize "Q" in datetime string. 
        -- First order by just the year, then order by the quarter number (should be last character in table name)
        v_sql := v_sql || format('
            ORDER BY to_timestamp(substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) for 4), ''YYYY'') %s
            , substring(reverse(c.relname) from 1 for 1) %s'
            , p_order
            , p_order);

    END IF;

ELSIF v_control_type = 'id' THEN

    v_sql := v_sql || format('
        ORDER BY substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) )::bigint %s'
        , p_order);

END IF;

RAISE DEBUG 'show_partitions: v_sql: %', v_sql;

RETURN QUERY EXECUTE v_sql;

END
$$;



CREATE OR REPLACE FUNCTION stop_sub_partition(p_parent_table text, p_jobmon boolean DEFAULT true) RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_job_id            bigint;
v_jobmon_schema     text;
v_step_id           bigint;

BEGIN
/*
 * Stop a given parent table from causing its children to be subpartitioned
 */

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT %I.add_job(''PARTMAN STOP SUBPARTITIONING'')', v_jobmon_schema) INTO v_job_id;
    EXECUTE format('SELECT %I.add_step(%s, ''Stopped subpartitioning for %s'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
END IF;

DELETE FROM @extschema@.part_config_sub WHERE sub_parent = p_parent_table;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT %I.update_step(%s, %L, %L)', v_jobmon_schema, v_step_id, 'OK', 'Done');
    EXECUTE format('SELECT %I.close_job(%s)', v_jobmon_schema, v_job_id);
END IF;

RETURN true;

END
$$;



CREATE FUNCTION @extschema@.undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval text DEFAULT NULL, p_keep_table boolean DEFAULT true, p_lock_wait numeric DEFAULT 0, p_target_table text DEFAULT NULL, OUT partitions_undone int, OUT rows_undone bigint) RETURNS record
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
v_adv_lock              boolean;
v_batch_interval_id     bigint;
v_batch_interval_time   interval;
v_batch_loop_count      int := 0;
v_child_loop_total      bigint := 0;
v_child_table           text;
v_control               text;
v_control_type          text;
v_child_min_id          bigint;
v_child_min_time        timestamptz;
v_epoch                 text;
v_function_name         text;
v_jobmon                boolean;
v_jobmon_schema         text;
v_job_id                bigint;
v_inner_loop_count      int;
v_lock_iter             int := 1;
v_lock_obtained         boolean := FALSE;
v_new_search_path       text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_expression  text;
v_partition_interval    text;
v_partition_type        text;
v_relkind               char;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_sub_count             int;
v_target_schema         text;
v_target_tablename      text;
v_template_schema       text;
v_template_siblings     int;
v_template_table        text;
v_template_tablename    text;
v_total                 bigint := 0;
v_trig_name             text;
v_undo_count            int := 0;

BEGIN
/*
 * For native, moves data to new, target table since data cannot be moved to parent.
 *      Leaves old parent table as is and does not change name of new table.
 * For trigger-based, moves data to parent
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition_native'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition_native already running.';
    partitions_undone = -1;
    RETURN;
END IF;

IF p_parent_table = p_target_table THEN
    RAISE EXCEPTION 'Target table cannot be the same as the parent table';
END IF;

SELECT partition_interval::text
    , partition_type
    , control
    , jobmon
    , epoch
    , template_table
INTO v_partition_interval
    , v_partition_type
    , v_control
    , v_jobmon
    , v_epoch
    , v_template_table
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;

IF v_control IS NULL THEN
    RAISE EXCEPTION 'No configuration found for pg_partman for given parent table: %', p_parent_table;
END IF;

IF v_partition_type = 'native' AND p_target_table IS NULL THEN
    RAISE EXCEPTION 'Natively partitioned tables require setting the p_target_table option';
END IF;

SELECT n.nspname, c.relname, c.relkind
INTO v_parent_schema, v_parent_tablename, v_relkind 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_parent_tablename IS NULL THEN
    RAISE EXCEPTION 'Given parent table not found in system catalogs: %', p_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
    IF p_batch_interval IS NULL THEN
        v_batch_interval_time := v_partition_interval::interval;
    ELSE
        v_batch_interval_time := p_batch_interval::interval;
    END IF;
ELSIF v_control_type = 'id' THEN
    IF p_batch_interval IS NULL THEN
        v_batch_interval_id := v_partition_interval::bigint;
    ELSE
        v_batch_interval_id := p_batch_interval::bigint;
    END IF;
ELSE
    RAISE EXCEPTION 'Data type of control column in given partition set must be either data/time or integer.';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Check if any child tables are themselves partitioned or part of an inheritance tree. Prevent undo at this level if so.
-- Need to lock child tables at all levels before multi-level undo can be performed safely.
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table)
LOOP
    SELECT count(*) INTO v_sub_count
    FROM pg_catalog.pg_inherits i
    JOIN pg_catalog.pg_class c ON i.inhparent = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_row.partition_tablename::name
    AND n.nspname = v_row.partition_schemaname::name;
    IF v_sub_count > 0 THEN
        RAISE EXCEPTION 'Child table for this parent has child table(s) itself (%). Run undo partitioning on this table or remove inheritance first to ensure all data is properly moved to parent', v_row.partition_schemaname||'.'||v_row.partition_tablename;
    END IF;
END LOOP;

IF p_target_table IS NOT NULL THEN
    SELECT n.nspname, c.relname 
    INTO v_target_schema, v_target_tablename 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(p_target_table, '.', 1)::name
    AND c.relname = split_part(p_target_table, '.', 2)::name;
ELSE
    v_target_schema := v_parent_schema;
    v_target_tablename := v_parent_tablename;
END IF;

IF v_target_tablename IS NULL THEN
    RAISE EXCEPTION 'Given target table not found in system catalogs: %', p_target_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN UNDO PARTITIONING: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Undoing partitioning for table %s', p_parent_table));
END IF;

v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
    ELSE format('%I', v_control)
END;

-- Stops new time partitons from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;

IF v_partition_type != 'native' THEN
    -- Stop data going into child tables on non-native partition sets.

    v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
    v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);

    -- Double-check for proper object existence
    SELECT tgname INTO v_trig_name 
    FROM pg_catalog.pg_trigger t
    JOIN pg_catalog.pg_class c ON t.tgrelid = c.oid
    WHERE tgname = v_trig_name::name 
    AND c.relname = v_parent_tablename::name;

    SELECT proname INTO v_function_name FROM pg_catalog.pg_proc p JOIN pg_catalog.pg_namespace n ON p.pronamespace = n.oid WHERE n.nspname = v_parent_schema::name AND proname = v_function_name::name;

    IF v_trig_name IS NOT NULL THEN
        -- lockwait for trigger drop
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE format('LOCK TABLE ONLY %I.%I IN ACCESS EXCLUSIVE MODE NOWAIT', v_parent_schema, v_parent_tablename);
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
                RAISE NOTICE 'Unable to obtain lock on parent table to remove trigger';
                partitions_undone = -1;
                RETURN;
            END IF;
        END IF; -- END p_lock_wait IF
        EXECUTE format('DROP TRIGGER IF EXISTS %I ON %I.%I', v_trig_name, v_parent_schema, v_parent_tablename);
    END IF; -- END trigger IF
    v_lock_obtained := FALSE; -- reset for reuse later

    IF v_function_name IS NOT NULL THEN
        EXECUTE format('DROP FUNCTION IF EXISTS %I.%I()', v_parent_schema, v_function_name);
    END IF;

END IF; -- end pg_partman trigger cleanup

IF v_jobmon_schema IS NOT NULL THEN
    IF (v_trig_name IS NOT NULL OR v_function_name IS NOT NULL) THEN
        PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
    ELSE
        PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process.');
    END IF;
END IF;

<<outer_child_loop>>
LOOP
    -- Get ordered list of child table in set. Store in variable one at a time per loop until none are left or batch count is reached.
    -- This easily allows it to loop over same child table until empty or move onto next child table after it's dropped
    -- Include the native default table to ensure all data there is removed as well (final parameter = true)
    SELECT partition_tablename INTO v_child_table FROM @extschema@.show_partitions(p_parent_table, 'ASC', TRUE) LIMIT 1;

    EXIT outer_child_loop WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Removing child partition: %s.%s', v_parent_schema, v_child_table));
    END IF;

    IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
        EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_time;
    ELSIF v_control_type = 'id' THEN
        EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_id;
    END IF;

    IF v_child_min_time IS NULL AND v_child_min_id IS NULL THEN
        -- No rows left in this child table. Remove from partition set.

        -- lockwait timeout for table drop
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE format('LOCK TABLE ONLY %I.%I IN ACCESS EXCLUSIVE MODE NOWAIT', v_parent_schema, v_child_table);
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
                RAISE NOTICE 'Unable to obtain lock on child table for removal from partition set';
                partitions_undone = -1;
                RETURN;
            END IF;
        END IF; -- END p_lock_wait IF
        v_lock_obtained := FALSE; -- reset for reuse later

        IF v_partition_type = 'native' THEN
            EXECUTE format('ALTER TABLE %I.%I DETACH PARTITION %I.%I'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_parent_schema
                            , v_child_table);
        ELSE
            EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                            , v_parent_schema
                            , v_child_table
                            , v_parent_schema
                            , v_parent_tablename);
        END IF;

        IF p_keep_table = false THEN
            EXECUTE format('DROP TABLE %I.%I', v_parent_schema, v_child_table);
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Child table DROPPED. Moved %s rows to target table', v_child_loop_total));
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Child table DETACHED/UNINHERITED from parent, not DROPPED. Moved %s rows to target table', v_child_loop_total));
            END IF;
        END IF;

        IF v_partition_type = 'time-custom' THEN
            DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_parent_schema||'.'||v_child_table;
        END IF;

        v_undo_count := v_undo_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
        CONTINUE outer_child_loop; -- skip data moving steps below
    END IF;
    v_inner_loop_count := 1;
    v_child_loop_total := 0;
    <<inner_child_loop>>
    LOOP
        IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
            -- do some locking with timeout, if required
            IF p_lock_wait > 0  THEN
                v_lock_iter := 0;
                WHILE v_lock_iter <= 5 LOOP
                    v_lock_iter := v_lock_iter + 1;
                    BEGIN
                        EXECUTE format('SELECT * FROM %I.%I WHERE %I <= %L FOR UPDATE NOWAIT'
                            , v_parent_schema
                            , v_child_table
                            , v_control
                            , v_child_min_time + (v_batch_interval_time * v_inner_loop_count));
                       v_lock_obtained := TRUE;
                    EXCEPTION
                        WHEN lock_not_available THEN
                            PERFORM pg_sleep( p_lock_wait / 5.0 );
                            CONTINUE;
                    END;
                    EXIT WHEN v_lock_obtained;
                END LOOP;
                IF NOT v_lock_obtained THEN
                    RAISE NOTICE 'Unable to obtain lock on batch of rows to move';
                    partitions_undone = -1;
                    RETURN;
                END IF;
            END IF;

            -- Get everything from the current child minimum up to the multiples of the given interval
            EXECUTE format('WITH move_data AS (
                                    DELETE FROM %I.%I WHERE %s <= %L RETURNING *)
                                  INSERT INTO %I.%I SELECT * FROM move_data'
                , v_parent_schema
                , v_child_table
                , v_partition_expression
                , v_child_min_time + (v_batch_interval_time * v_inner_loop_count)
                , v_target_schema
                , v_target_tablename);
            GET DIAGNOSTICS v_rowcount = ROW_COUNT;
            v_total := v_total + v_rowcount;
            v_child_loop_total := v_child_loop_total + v_rowcount;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Moved %s rows to target table.', v_child_loop_total));
            END IF;
            EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
            v_inner_loop_count := v_inner_loop_count + 1;
            v_batch_loop_count := v_batch_loop_count + 1;

            -- Check again if table is empty and go to outer loop again to drop it if so
            EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_time;
            CONTINUE outer_child_loop WHEN v_child_min_time IS NULL;
            
        ELSIF v_control_type = 'id' THEN

            IF p_lock_wait > 0  THEN
                v_lock_iter := 0;
                WHILE v_lock_iter <= 5 LOOP
                    v_lock_iter := v_lock_iter + 1;
                    BEGIN
                        EXECUTE format('SELECT * FROM %I.%I WHERE %I <= %L FOR UPDATE NOWAIT'
                            , v_parent_schema
                            , v_child_table
                            , v_control
                            , v_child_min_id + (v_batch_interval_id * v_inner_loop_count));
                       v_lock_obtained := TRUE;
                    EXCEPTION
                        WHEN lock_not_available THEN
                            PERFORM pg_sleep( p_lock_wait / 5.0 );
                            CONTINUE;
                    END;
                    EXIT WHEN v_lock_obtained;
                END LOOP;
                IF NOT v_lock_obtained THEN
                   RAISE NOTICE 'Unable to obtain lock on batch of rows to move';
                   partitions_undone = -1;
                   RETURN;
                END IF;
            END IF;

            -- Get everything from the current child minimum up to the multiples of the given interval
            EXECUTE format('WITH move_data AS (
                                    DELETE FROM %I.%I WHERE %s <= %L RETURNING *)
                                  INSERT INTO %I.%I SELECT * FROM move_data'
                , v_parent_schema
                , v_child_table
                , v_partition_expression
                , v_child_min_id + (v_batch_interval_id * v_inner_loop_count)
                , v_target_schema
                , v_target_tablename);
            GET DIAGNOSTICS v_rowcount = ROW_COUNT;
            v_total := v_total + v_rowcount;
            v_child_loop_total := v_child_loop_total + v_rowcount;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Moved %s rows to target table.', v_child_loop_total));
            END IF;
            EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
            v_inner_loop_count := v_inner_loop_count + 1;
            v_batch_loop_count := v_batch_loop_count + 1;

            -- Check again if table is empty and go to outer loop again to drop it if so
            EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_id;
            CONTINUE outer_child_loop WHEN v_child_min_id IS NULL;

        END IF; -- end v_control_type check

        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached

    END LOOP inner_child_loop;
END LOOP outer_child_loop;

SELECT partition_tablename INTO v_child_table FROM @extschema@.show_partitions(p_parent_table, 'ASC', TRUE) LIMIT 1;

IF v_child_table IS NULL THEN
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    
    -- Check if any other config entries still have this template table and don't remove if so
    -- Allows other sibling/parent tables to still keep using in case entire partition set isn't being undone
    SELECT count(*) INTO v_template_siblings FROM @extschema@.part_config WHERE template_table = v_template_table;

    SELECT n.nspname, c.relname
    INTO v_template_schema, v_template_tablename
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(v_template_table, '.', 1)::name
    AND c.relname = split_part(v_template_table, '.', 2)::name;

    IF v_template_siblings = 0 AND v_template_tablename IS NOT NULL THEN
        EXECUTE format('DROP TABLE IF EXISTS %I.%I', v_template_schema, v_template_tablename);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Moved % row(s) to the target table. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', format('Moved %s row(s) to the target table. Removed %s partitions.', v_total, v_undo_count));
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

partitions_undone := v_undo_count;
rows_undone := v_total;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN UNDO PARTITIONING: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;



-- Restore dropped object privileges
DO $$
DECLARE
v_row   record;
BEGIN
    FOR v_row IN SELECT statement FROM partman_preserve_privs_temp LOOP
        IF v_row.statement IS NOT NULL THEN
            EXECUTE v_row.statement;
        END IF;
    END LOOP;
END
$$;

DROP TABLE IF EXISTS partman_preserve_privs_temp;
