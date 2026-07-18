-- New functions that can manage additional constraints on child tables older than premake value based on what their min/max values are. This allows constraint exclusion to become usable on columns other than the partitioning column. However, this is only useful if older tables are no longer editing the columns that will have constraints placed on them, otherwise you risk constraint violations. See blog post for a more thorough explanation and examples: http://www.keithf4.com/managing-constraint-exclusion-in-table-partitioning
-- New python script for using above functions to drop/apply constraints on an entire partition set without causing excessive lock issues.
-- show_partitions() function is now guarenteed to return a list of partitions in the order that makes sense for the partition type/interval for the set. Additional option to specify ascending or descending order (defaults to ascending).
-- Check for valid parameter values in partition creation function (github issue #14 from Josh Berkus)
-- Added drop index concurrently option (--drop_concurrently) to reapply_indexes.py script. Only works for 9.2+ 
-- Changed run_maintenance() to use advisory transaction lock instead of session level lock.
-- Fixed missing library import in python scripts.
-- Organized docmentation of functions.


ALTER TABLE @extschema@.part_config ADD constraint_cols text[];

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_parent(text, text, text, text, text[], int, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_parent'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.show_partitions(text, text) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'show_partitions'; 

CREATE FUNCTION replay_preserved_privs() RETURNS void
    LANGUAGE plpgsql
    AS $$
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

DROP FUNCTION @extschema@.create_parent(text, text, text, text, int, boolean); 
DROP FUNCTION @extschema@.show_partitions(text);

/* 
 * Apply constraints managed by partman extension
 */
CREATE FUNCTION apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_debug BOOLEAN DEFAULT FALSE) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_child_table                   text;
v_child_tablename               text;
v_col                           text;
v_constraint_cols               text[];
v_constraint_col_type           text;
v_constraint_name               text;
v_datetime_string               text;
v_existing_constraint_name      text;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             int; 
v_last_partition_timestamp      timestamp;
v_constraint_values             record;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 text;
v_partition_suffix              text;
v_premake                       int;
v_sql                           text;
v_step_id                       bigint;
v_suffix_position               int;
v_type                          text;

BEGIN

SELECT type
    , part_interval
    , premake
    , datetime_string
    , last_partition
    , constraint_cols
INTO v_type
    , v_part_interval
    , v_premake
    , v_datetime_string
    , v_last_partition
    , v_constraint_cols
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF v_constraint_cols IS NULL THEN
    IF p_debug THEN
        RAISE NOTICE 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    END IF;
    -- Returns silently to allow this function to be simply called by maintenance processes without having to check if config options are set.
    RETURN;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

-- If p_child_table is null, figure out the partition that is the one right before the premake value backwards.
IF p_child_table IS NULL THEN
    
    v_suffix_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;

    IF v_type IN ('time-static', 'time-dynamic') THEN
        v_last_partition_timestamp := to_timestamp(substring(v_last_partition from v_suffix_position), v_datetime_string);
        v_partition_suffix := to_char(v_last_partition_timestamp - (v_part_interval::interval * ((v_premake * 2)+1) ), v_datetime_string);
    ELSIF v_type IN ('id-static', 'id-dynamic') THEN
        v_last_partition_id := substring(v_last_partition from v_suffix_position)::int;
        v_partition_suffix := (v_last_partition_id - (v_part_interval::int * ((v_premake * 2)+1) ))::text; 
    END IF;
    
    v_child_table := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);

ELSE
    v_child_table := p_child_table;
END IF;
    
SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE CONSTRAINT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Checking if target child table exists');
END IF;

SELECT tablename INTO v_child_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_child_table;
IF v_child_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'NOTICE', 'Target child table ('||v_child_table||') does not exist. Skipping constraint creation.');
        PERFORM close_job(v_job_id);
        EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Target child table (%) does not exist. Skipping constraint creation.', v_child_table;
    END IF;
    RETURN;
ELSE
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

FOREACH v_col IN ARRAY v_constraint_cols
LOOP
    SELECT c.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint c 
        JOIN pg_catalog.pg_attribute a ON c.conrelid = a.attrelid 
    WHERE conrelid = v_child_table::regclass 
        AND c.conname LIKE 'partmanconstr_%'
        AND c.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] <@ c.conkey 
        AND a.attisdropped = false;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying new constraint on column: '||v_col);
    END IF;

    IF v_existing_constraint_name IS NOT NULL THEN
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', 'Partman managed constraint already exists on this table ('||v_child_table||') and column ('||v_col||'). Skipping creation.');
        END IF;
        RAISE WARNING 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_table, v_col ;
        CONTINUE;
    END IF;

    -- Ensure column name gets put on end of constraint name to help avoid naming conflicts 
    v_constraint_name := @extschema@.check_name_length('partmanconstr_'||v_child_tablename, p_suffix := '_'||v_col);

    EXECUTE 'SELECT min('||v_col||')::text AS min, max('||v_col||')::text AS max FROM '||v_child_table INTO v_constraint_values;

    IF v_constraint_values IS NOT NULL THEN
        v_sql := concat('ALTER TABLE ', v_child_table, ' ADD CONSTRAINT ', v_constraint_name
            , ' CHECK (', v_col, ' >= ', quote_literal(v_constraint_values.min), ' AND '
            , v_col, ' <= ', quote_literal(v_constraint_values.max), ')' );
        IF p_debug THEN
            RAISE NOTICE 'Constraint creation query: %', v_sql;
        END IF;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'New constraint created: '||v_sql);
        END IF;
    ELSE
        IF p_debug THEN
            RAISE NOTICE 'Given column (%) contains all NULLs. No constraint created', v_col;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', 'Given column ('||v_col||') contains all NULLs. No constraint created');
        END IF;
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE CONSTRAINT: '||p_parent_table||')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition constraint maintenance for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Drop constraints managed by pg_partman
 */
CREATE FUNCTION drop_constraints(p_parent_table text, p_child_table text, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_col                           text;
v_constraint_cols               text[]; 
v_existing_constraint_name      text;
v_exists                        boolean := FALSE;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_sql                           text;
v_step_id                       bigint;

BEGIN

SELECT constraint_cols INTO v_constraint_cols FROM @extschema@.part_config WHERE parent_table = p_parent_table;

IF v_constraint_cols IS NULL THEN
    RAISE EXCEPTION 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN DROP CONSTRAINT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Entering constraint drop loop');
    PERFORM update_step(v_step_id, 'OK', 'Done');
END IF;


FOREACH v_col IN ARRAY v_constraint_cols
LOOP

    SELECT c.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint c 
        JOIN pg_catalog.pg_attribute a ON c.conrelid = a.attrelid 
    WHERE conrelid = p_child_table::regclass 
        AND c.conname LIKE 'partmanconstr_%'
        AND c.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] <@ c.conkey 
        AND a.attisdropped = false;

    IF v_existing_constraint_name IS NOT NULL THEN
        v_exists := TRUE;
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Dropping constraint on column: '||v_col);
        END IF;
        v_sql := 'ALTER TABLE '||p_child_table||' DROP CONSTRAINT '||v_existing_constraint_name;
        IF p_debug THEN
            RAISE NOTICE 'Constraint drop query: %', v_sql;
        END IF;
        EXECUTE v_sql;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Drop constraint query: '||v_sql);
        END IF;
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL AND v_exists IS FALSE THEN
    v_step_id := add_step(v_job_id, 'No constraints found to drop on child table: '||p_child_table);
    PERFORM update_step(v_step_id, 'OK', 'Done');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN DROP CONSTRAINT: '||p_parent_table||')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition constraint maintenance for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Create the trigger function for the parent table of an id-based partition set
 */
CREATE OR REPLACE FUNCTION create_id_function(p_parent_table text, p_current_id bigint) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_function_name                 text;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition                text;
v_next_partition_id             bigint;
v_next_partition_name           text;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 bigint;
v_premake                       int;
v_prev_partition_id             bigint;
v_prev_partition_name           text;
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT type
    , part_interval::bigint
    , control
    , premake
    , last_partition
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

IF v_type = 'id-static' THEN
    v_current_partition_id := p_current_id - (p_current_id % v_part_interval);
    v_next_partition_id := v_current_partition_id + v_part_interval;
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_current_partition_id::text, TRUE);

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_current_partition_id  bigint;
            v_last_partition        text := '||quote_literal(v_last_partition)||';
            v_id_position           int;
            v_next_partition_id     bigint;
            v_next_partition_name   text;         
        BEGIN
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||v_current_partition_id||' AND NEW.'||v_control||' < '||v_next_partition_id|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); ';

        FOR i IN 1..v_premake LOOP
            v_prev_partition_id := v_current_partition_id - (v_part_interval * i);
            v_next_partition_id := v_current_partition_id + (v_part_interval * i);
            v_final_partition_id := v_next_partition_id + v_part_interval;
            v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_prev_partition_id::text, TRUE);
            v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_next_partition_id::text, TRUE);
            
            -- Check that child table exist before making a rule to insert to them.
            -- Handles edge case of changing premake immediately after running create_parent(). 
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_prev_partition_name;
            IF v_count > 0 THEN
                -- Only handle previous partitions if they're starting above zero
                IF v_prev_partition_id >= 0 THEN
                    v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_prev_partition_id||' AND NEW.'||v_control||' < '||v_prev_partition_id + v_part_interval|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); ';
                END IF;
            END IF;
            
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_next_partition_name;
            IF v_count > 0 THEN
                v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_next_partition_id||' AND NEW.'||v_control||' < '||v_final_partition_id|| ' THEN 
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*); ';
            END IF;
        END LOOP;

        v_trig_func := v_trig_func ||'
            ELSE
                RETURN NEW;
            END IF;
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_next_partition_id := (substring(v_last_partition from v_id_position)::bigint) + '||v_part_interval||';
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||v_part_interval||', ARRAY[v_next_partition_id]);
                    UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                    PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                    PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;
        END IF; 
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current id interval: '||v_current_partition_id||' to '||v_final_partition_id-1);
    END IF;

ELSIF v_type = 'id-dynamic' THEN
    -- The return inside the partition creation check is there to keep really high ID values from creating new partitions.
    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_id_position               int;
            v_last_partition            text := '||quote_literal(v_last_partition)||';
            v_last_partition_id         bigint;
            v_next_partition_id         bigint;
            v_next_partition_name       text;   
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            v_current_partition_name := @extschema@.check_name_length('''||v_parent_tablename||''', '''||v_parent_schema||''', v_current_partition_id::text, TRUE);
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
                v_next_partition_id := v_last_partition_id + '||v_part_interval||';
                IF NEW.'||v_control||' >= v_next_partition_id THEN
                    RETURN NEW;
                END IF;
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||quote_literal(v_part_interval)||', ARRAY[v_next_partition_id]);
                    IF v_next_partition_name IS NOT NULL THEN
                        UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                        PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                        PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    END IF;
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname ||''.''|| tablename = v_current_partition_name;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_current_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;
        END IF;
        
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for dynamic id table: '||p_parent_table);
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid id partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE FUNCTION: '||p_parent_table||')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition function maintenance for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to manage pre-creation of the next partitions in a time-based partition set.
 * Also manages dropping old partitions if the retention option is set.
 */
CREATE OR REPLACE FUNCTION run_maintenance() RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                      boolean;
v_create_count                  int := 0;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_drop_count                    int := 0;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_timestamp      timestamp;
v_old_search_path               text;
v_premade_count                 int;
v_quarter                       text;
v_step_id                       bigint;
v_row                           record;
v_time_position                 int;
v_year                          text;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

FOR v_row IN 
SELECT parent_table
    , type
    , part_interval::interval
    , control
    , premake
    , datetime_string
    , last_partition
    , undo_in_progress
FROM @extschema@.part_config WHERE type = 'time-static' OR type = 'time-dynamic'
LOOP

    CONTINUE WHEN v_row.undo_in_progress;
    
    CASE
        WHEN v_row.part_interval = '15 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_row.part_interval = '30 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_row.part_interval = '1 hour' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_row.part_interval = '1 day' THEN
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 week' THEN
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 month' THEN
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '3 months' THEN
            v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 year' THEN
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;

    -- Get position of last occurance of '_p' in child partition name
    v_time_position := (length(v_row.last_partition) - position('p_' in reverse(v_row.last_partition))) + 2;
    IF v_row.part_interval != '3 months' THEN
       v_last_partition_timestamp := to_timestamp(substring(v_row.last_partition from v_time_position), v_row.datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(substring(v_row.last_partition from v_time_position), 'q', 1);
        v_quarter := split_part(substring(v_row.last_partition from v_time_position), 'q', 2);
        CASE
            WHEN v_quarter = '1' THEN
                v_last_partition_timestamp := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
            WHEN v_quarter = '2' THEN
                v_last_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
            WHEN v_quarter = '3' THEN
                v_last_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
            WHEN v_quarter = '4' THEN
                v_last_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        END CASE;
    END IF;

    -- Check and see how many premade partitions there are.
    v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
    -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
    WHILE v_premade_count < v_row.premake LOOP
        EXECUTE 'SELECT @extschema@.create_next_time_partition('||quote_literal(v_row.parent_table)||')';
        v_create_count := v_create_count + 1;
        IF v_row.type = 'time-static' THEN
            EXECUTE 'SELECT @extschema@.create_time_function('||quote_literal(v_row.parent_table)||')';
        END IF;

        -- Manage additonal constraints if set
        PERFORM @extschema@.apply_constraints(v_row.parent_table);

        v_last_partition_timestamp := v_last_partition_timestamp + v_row.part_interval;
        v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
    END LOOP;

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'time-static' OR type = 'time-dynamic')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'id-static' OR type = 'id-dynamic')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_create_count||' partitions made. '||v_drop_count||' partitions dropped.');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN RUN MAINTENANCE'')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before job logging started'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to turn a table into the parent of a partition set
 */
CREATE FUNCTION create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_debug boolean DEFAULT false) 
RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_current_id            bigint;
v_datetime_string       text;
v_id_interval           bigint;
v_job_id                bigint;
v_jobmon_schema         text;
v_last_partition_name   text;
v_old_search_path       text;
v_partition_time        timestamp[];
v_partition_id          bigint[];
v_max                   bigint;
v_notnull               boolean;
v_starting_partition_id bigint;
v_step_id               bigint;
v_tablename             text;
v_time_interval         interval;
v_valid_types           text[] := '{"time-static", "time-dynamic", "id-static", "id-dynamic"}';

BEGIN

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
    END IF;

SELECT attnotnull INTO v_notnull FROM pg_attribute WHERE attrelid = p_parent_table::regclass AND attname = p_control;
    IF v_notnull = false OR v_notnull IS NULL THEN
        RAISE EXCEPTION 'Control column (%) for parent table (%) must be NOT NULL', p_control, p_parent_table;
    END IF;

IF NOT (string_to_array(p_type, '') <@ v_valid_types) THEN
    RAISE EXCEPTION '% is not a valid partitioning type (%)', p_type, array_to_string(v_valid_types, ', ');
END IF;

EXECUTE 'LOCK TABLE '||p_parent_table||' IN ACCESS EXCLUSIVE MODE';

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN SETUP PARENT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating initial partitions on new parent table: '||p_parent_table);
END IF;

CASE
    WHEN p_interval = 'yearly' THEN
        v_time_interval = '1 year';
        v_datetime_string := 'YYYY';
    WHEN p_interval = 'quarterly' THEN
        v_time_interval = '3 months';
        v_datetime_string = 'YYYY"q"Q';
    WHEN p_interval = 'monthly' THEN
        v_time_interval = '1 month';
        v_datetime_string := 'YYYY_MM';
    WHEN p_interval = 'weekly' THEN
        v_time_interval = '1 week';
        v_datetime_string := 'IYYY"w"IW';
    WHEN p_interval = 'daily' THEN
        v_time_interval = '1 day';
        v_datetime_string := 'YYYY_MM_DD';
    WHEN p_interval = 'hourly' THEN
        v_time_interval = '1 hour';
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
    WHEN p_interval = 'half-hour' THEN
        v_time_interval = '30 mins';
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
    WHEN p_interval = 'quarter-hour' THEN
        v_time_interval = '15 mins';
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
    ELSE
        IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
            v_id_interval := p_interval::bigint;
            IF v_id_interval <= 0 THEN
                RAISE EXCEPTION 'Interval for serial partitioning must be greater than zero';
            END IF;
        ELSE
            RAISE EXCEPTION 'Invalid interval for time based partitioning: %', p_interval;
        END IF;
END CASE;

IF p_type = 'time-static' OR p_type = 'time-dynamic' THEN
    FOR i IN 0..p_premake LOOP
        IF i > 0 THEN -- also create previous partitions equal to premake, but avoid duplicating current
            v_partition_time := array_append(v_partition_time, quote_literal(CURRENT_TIMESTAMP - (v_time_interval*i))::timestamp);
        END IF;
        v_partition_time := array_append(v_partition_time, quote_literal(CURRENT_TIMESTAMP + (v_time_interval*i))::timestamp);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, datetime_string) VALUES
        (p_parent_table, p_type, v_time_interval, p_control, p_premake, v_datetime_string);
    EXECUTE 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(p_control)||','
        ||quote_literal(v_time_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_time)||')' INTO v_last_partition_name;
    -- Doing separate update because create function needs parent table in config table for apply_grants()
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time partitions premade: '||p_premake);
    END IF;
END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    -- If there is already data, start partitioning with the highest current value
    EXECUTE 'SELECT COALESCE(max('||p_control||')::bigint, 0) FROM '||p_parent_table||' LIMIT 1' INTO v_max;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive
        IF (v_starting_partition_id - (v_id_interval*i)) > 0 AND (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id THEN
            v_partition_id = array_append(v_partition_id, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id = array_append(v_partition_id, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols) VALUES
        (p_parent_table, p_type, v_id_interval, p_control, p_premake, p_constraint_cols);
    EXECUTE 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(p_control)||','
        ||v_id_interval||','||quote_literal(v_partition_id)||')' INTO v_last_partition_name;
    -- Doing separate update because create function needs parent table in config table for apply_grants()
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID partitions premade: '||p_premake);
    END IF;
    
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition function');
END IF;

IF p_type = 'time-static' OR p_type = 'time-dynamic' THEN
    EXECUTE 'SELECT @extschema@.create_time_function('||quote_literal(p_parent_table)||')';
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time function created');
    END IF;
ELSIF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    v_current_id := COALESCE(v_max, 0);    
    EXECUTE 'SELECT @extschema@.create_id_function('||quote_literal(p_parent_table)||','||v_current_id||')';  
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID function created');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition trigger');
END IF;

EXECUTE 'SELECT @extschema@.create_trigger('||quote_literal(p_parent_table)||')';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE PARENT: '||p_parent_table||')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition creation for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to list all child partitions in a set.
 * Will list all child tables in any inheritance set, 
 * not just those managed by pg_partman.
 */
CREATE FUNCTION show_partitions (p_parent_table text, p_order text DEFAULT 'ASC') RETURNS SETOF text
    LANGUAGE plpgsql STABLE SECURITY DEFINER 
    AS $$
DECLARE

v_datetime_string   text;
v_part_interval     text;  
v_type              text;

BEGIN

IF p_order NOT IN ('ASC', 'DESC') THEN
    RAISE EXCEPTION 'p_order paramter must be one of the following values: ASC, DESC';
END IF;

SELECT type
    , part_interval
    , datetime_string
INTO v_type
    , v_part_interval
    , v_datetime_string
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF v_type IN ('time-static', 'time-dynamic') THEN

    RETURN QUERY EXECUTE '
    SELECT n.nspname::text ||''.''|| c.relname::text AS partition_name FROM
    pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhparent = '||quote_literal(p_parent_table)||'::regclass
    ORDER BY to_timestamp(substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) ), '||quote_literal(v_datetime_string)||') ' || p_order;

ELSIF v_type IN ('id-static', 'id-dynamic') THEN
    
    RETURN QUERY EXECUTE '
    SELECT n.nspname::text ||''.''|| c.relname::text AS partition_name FROM
    pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhparent = '||quote_literal(p_parent_table)||'::regclass
    ORDER BY substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) )::int ' || p_order;

END IF;

END
$$;

SELECT @extschema@.replay_preserved_privs();
DROP FUNCTION @extschema@.replay_preserved_privs();
DROP TABLE partman_preserve_privs_temp;
