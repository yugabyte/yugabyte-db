/*
 * Function to drop child tables from a time-based partition set. Options to drop indexes or actually drop the table from the database.
 */
CREATE FUNCTION drop_id_partition(p_parent_table text, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                  boolean;
v_child_table               text;
v_control                   text;
v_drop_count                int := 0;
v_index                     record;
v_job_id                    bigint;
v_jobmon_schema             text;
v_max                       bigint;
v_old_search_path           text;
v_part_interval             bigint;
v_partition_id              bigint;
v_retention                 bigint;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_step_id                   bigint;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman drop_id_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_id_partition already running.';
    RETURN 0;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

SELECT  
    part_interval::bigint
    , control
    , retention::bigint
    , retention_keep_table
    , retention_keep_index
INTO
    v_part_interval
    , v_control
    , v_retention
    , v_retention_keep_table
    , v_retention_keep_index
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'id-static' OR type = 'id-dynamic') 
AND retention IS NOT NULL;

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

-- Allow override of keeping tables or indexes from input parameters
IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN DROP ID PARTITION: '|| p_parent_table);
END IF;

EXECUTE 'SELECT max('||v_control||') FROM '||p_parent_table INTO v_max;

-- Loop through child tables of the given parent
FOR v_child_table IN 
    SELECT inhrelid::regclass FROM pg_catalog.pg_inherits WHERE inhparent::regclass = p_parent_table::regclass ORDER BY inhrelid::regclass ASC
LOOP
    v_partition_id := substring(v_child_table from char_length(p_parent_table||'_p')+1)::bigint;

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention <= (v_max - (v_partition_id + v_part_interval)) THEN
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Uninherit table '||v_child_table||' from '||p_parent_table);
        END IF;
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_keep_table = false THEN
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, 'Drop table '||v_child_table);
            END IF;
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        ELSIF v_retention_keep_index = false THEN
            FOR v_index IN 
                SELECT i.indexrelid::regclass AS name
                , c.conname
                FROM pg_catalog.pg_index i
                LEFT JOIN pg_catalog.pg_constraint c ON i.indexrelid = c.conindid 
                WHERE i.indrelid = v_child_table::regclass
            LOOP
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, 'Drop index '||v_index.name||' from '||v_child_table);
                END IF;
                IF v_index.conname IS NOT NULL THEN
                    EXECUTE 'ALTER TABLE '||v_child_table||' DROP CONSTRAINT '||v_index.conname;
                ELSE
                    EXECUTE 'DROP INDEX '||v_index.name;
                END IF;
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            END LOOP;
        END IF;
        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
    PERFORM update_step(v_step_id, 'OK', v_drop_count||' partitions dropped.');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman drop_id_partition'));

RETURN v_drop_count;

EXCEPTION
    WHEN QUERY_CANCELED THEN
        PERFORM pg_advisory_unlock(hashtext('pg_partman drop_id_partition'));
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN DROP ID PARTITION');
                v_step_id := add_step(v_job_id, 'EXCEPTION before job logging started');
            END IF;
            IF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        PERFORM pg_advisory_unlock(hashtext('pg_partman drop_id_partition'));
        RAISE EXCEPTION '%', SQLERRM;
END
$$;
