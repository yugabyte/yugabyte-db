-- Updated creation of child partition, function & trigger names to take into account the max object length an object can have to guarentee the partition suffix. Involved extensive rewrite of many core functions.
-- WARNING: If your table names were already long enough to be causing name truncation (over 63 characters), you may get duplicate child tables, functions & triggers created. Please check your object name lengths on your partition sets before installing this update to see if you may be affected by this edge case and its subsequent fix. 
-- New python script (reapply-indexes.py) to re-apply indexes to child tables when they have changed on the parent. See docs for more info.
-- New function to check the uniqueness of a column in a partition set (check_unique_column()). Helps to overcome the inability of a unique constraint to be applied efficiently across all partitions in a set. Does not prevent a unique violation, but provides a method to monitor for it happening.
-- More pgTAP tests to ensure name trunucation process is working. 
-- Changed pgTAP tests to assume pgTAP is installed in public schema to try and avoid issues when it isn't.


/*
 * Truncate the name of the given object if it is greater than the postgres default max (63 characters).
 * Also appends given suffix and schema if given and truncates the name so that the entire suffix will fit.
 * Returns original name with schema given if it doesn't require truncation
 */
CREATE FUNCTION check_name_length (p_object_name text, p_object_schema text DEFAULT NULL, p_suffix text DEFAULT NULL, p_table_partition boolean DEFAULT FALSE) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE
    v_new_length    int;
    v_new_name      text;
BEGIN

IF p_table_partition IS TRUE AND (p_suffix IS NULL OR p_object_schema IS NULL) THEN
    RAISE EXCEPTION 'Table partition name requires a schema and suffix value';
END IF;

IF p_table_partition THEN  -- 61 characters to account for _p in partition name
    IF char_length(p_object_name) + char_length(p_suffix) >= 61 THEN
        v_new_length := 61 - char_length(p_suffix);
        v_new_name := p_object_schema ||'.'|| substring(p_object_name from 1 for v_new_length) || '_p' || p_suffix; 
    ELSE
        v_new_name := p_object_schema ||'.'||p_object_name||'_p'||p_suffix;
    END IF;
ELSE
    IF char_length(p_object_name) + char_length(COALESCE(p_suffix, '')) >= 63 THEN
        v_new_length := 63 - char_length(COALESCE(p_suffix, ''));
        v_new_name := COALESCE(p_object_schema ||'.', '') || substring(p_object_name from 1 for v_new_length) || COALESCE(p_suffix, ''); 
    ELSE
        v_new_name := COALESCE(p_object_schema ||'.', '') || p_object_name||COALESCE(p_suffix, '');
    END IF;
END IF;

RETURN v_new_name;

END
$$;


CREATE TYPE check_unique_table AS (column_value text, count bigint);
/*
 * Function to check uniqueness of a column in a partition set.
 * First draft that runs within database in a single transaction. 
 * Working on version that will dump data out to perform a quicker check with less impact on DB.
 */
CREATE FUNCTION check_unique_column(p_parent_table text, p_column text) RETURNS SETOF check_unique_table
    LANGUAGE plpgsql
    AS $$
DECLARE

    v_row       record;
    v_sql       text;
    v_trouble   @extschema@.check_unique_table%rowtype;

BEGIN

v_sql := 'SELECT '||p_column||'::text AS column_value, count('||p_column||') AS count
        FROM '||p_parent_table||' GROUP BY '||p_column||' HAVING (count('||p_column||') > 1) ORDER BY '||p_column;

RAISE NOTICE 'v_sql: %', v_sql;

FOR v_row IN EXECUTE v_sql
LOOP
    v_trouble.column_value := v_row.column_value;
    v_trouble.count := v_row.count;
    RETURN NEXT v_trouble;
END LOOP;

END
$$;


/*
 * Function to create id partitions
 */
CREATE OR REPLACE FUNCTION create_id_partition (p_parent_table text, p_control text, p_interval bigint, p_partition_ids bigint[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_grantees          text[];
v_job_id            bigint;
v_jobmon_schema     text;
v_old_search_path   text;
v_parent_grant      record;
v_parent_owner      text;
v_parent_schema     text;
v_parent_tablename  text;
v_partition_name    text;
v_revoke            text[];
v_step_id           bigint;
v_tablename         text;
v_id                bigint;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

SELECT tableowner, schemaname, tablename INTO v_parent_owner, v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_id IN ARRAY p_partition_ids LOOP
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_id::text, TRUE);

    -- If child table already exists, skip creation
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + p_interval)-1);
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
        CHECK ('||p_control||'>='||quote_literal(v_id)||' AND '||p_control||'<'||quote_literal(v_id + p_interval)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'BAD', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_partition (p_parent_table text, p_control text, p_interval interval, p_datetime_string text, p_partition_times timestamp[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_grantees                      text[];
v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_parent_grant                  record;
v_parent_owner                  text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_name                text;
v_partition_suffix              text;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text[];
v_step_id                       bigint;
v_tablename                     text;
v_trunc_value                   text;
v_time                          timestamp;
v_year                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

SELECT tableowner, schemaname, tablename INTO v_parent_owner, v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_time IN ARRAY p_partition_times LOOP    

    IF p_interval <= '1 year' AND p_interval <> '3 months' AND p_interval <> '1 week' THEN            
        v_partition_suffix := to_char(v_time, 'YYYY');
        v_trunc_value := 'year';

        IF p_interval <= '1 month' AND p_interval <> '1 week' THEN
            v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'MM');
            v_trunc_value := 'month';

            IF p_interval <= '1 day' THEN
                v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'DD');
                v_trunc_value := 'day';

                IF p_interval <= '1 hour' THEN
                    v_partition_suffix := v_partition_suffix || '_' || to_char(v_time, 'HH24');
                    IF p_interval <> '30 mins' AND p_interval <> '15 mins' THEN
                        v_partition_suffix := v_partition_suffix || '00';
                        v_trunc_value := 'hour';
                    ELSIF p_interval = '15 mins' THEN
                        IF date_part('minute', v_time) < 15 THEN
                            v_partition_suffix := v_partition_suffix || '00';
                        ELSIF date_part('minute', v_time) >= 15 AND date_part('minute', v_time) < 30 THEN
                            v_partition_suffix := v_partition_suffix || '15';
                        ELSIF date_part('minute', v_time) >= 30 AND date_part('minute', v_time) < 45 THEN
                            v_partition_suffix := v_partition_suffix || '30';
                        ELSE
                            v_partition_suffix := v_partition_suffix || '45';
                        END IF;
                        v_trunc_value := 'minute';
                    ELSIF p_interval = '30 mins' THEN
                        IF date_part('minute', v_time) < 30 THEN
                            v_partition_suffix := v_partition_suffix || '00';
                        ELSE
                            v_partition_suffix := v_partition_suffix || '30';
                        END IF;
                        v_trunc_value := 'minute';
                    END IF;
                END IF; -- end hour IF      
            END IF; -- end day IF
        END IF; -- end month IF
    ELSIF p_interval = '1 week' THEN
        v_partition_suffix := to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
        v_trunc_value := 'week';
    END IF; -- end year/week IF

    
    -- "Q" is ignored in to_timestamp, so handle special case
    IF p_interval = '3 months' THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_suffix := v_year || 'q' || v_quarter;
        v_trunc_value := 'quarter';
        CASE 
            WHEN v_quarter = '1' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-01-01', 'YYYY-MM-DD'));
            WHEN v_quarter = '2' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-04-01', 'YYYY-MM-DD'));
            WHEN v_quarter = '3' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-07-01', 'YYYY-MM-DD'));
            WHEN v_quarter = '4' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-10-01', 'YYYY-MM-DD'));
        END CASE;
        v_partition_timestamp_end := date_trunc(v_trunc_value, (v_partition_timestamp_start + p_interval));
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);

    -- pull out datetime portion of last partition's tablename if it matched anything except quarterly
    IF p_interval <> '3 months' AND v_trunc_value IS NOT NULL THEN
        v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_partition_suffix, p_datetime_string));
        v_partition_timestamp_end := date_trunc(v_trunc_value, to_timestamp(v_partition_suffix, p_datetime_string) + p_interval);
    END IF;

    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check
        CHECK ('||p_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||p_control||'<'||quote_literal(v_partition_timestamp_end)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
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

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
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

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
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
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Create the trigger function for the parent table of a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_function(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_timestamp   timestamptz;
v_datetime_string               text;
v_final_partition_timestamp     timestamptz;
v_function_name                 text;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_new_length                    int;
v_next_partition_name           text;
v_next_partition_timestamp      timestamptz;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 interval;
v_premake                       int;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamptz;
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT type
    , part_interval::interval
    , control
    , premake
    , datetime_string
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

IF v_type = 'time-static' THEN
    CASE
        WHEN v_part_interval = '15 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_part_interval = '30 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_part_interval = '1 hour' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_part_interval = '1 day' THEN
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 week' THEN
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 month' THEN
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '3 months' THEN
            v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 year' THEN
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;

    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, to_char(v_current_partition_timestamp, v_datetime_string), TRUE); 
    v_next_partition_timestamp := v_current_partition_timestamp + v_part_interval::interval;

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||quote_literal(v_current_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_next_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); ';
    FOR i IN 1..v_premake LOOP
        v_prev_partition_timestamp := v_current_partition_timestamp - (v_part_interval::interval * i);
        v_next_partition_timestamp := v_current_partition_timestamp + (v_part_interval::interval * i);
        v_final_partition_timestamp := v_next_partition_timestamp + (v_part_interval::interval);
        v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, to_char(v_prev_partition_timestamp, v_datetime_string), TRUE);
        v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, to_char(v_next_partition_timestamp, v_datetime_string), TRUE);

        -- Check that child table exist before making a rule to insert to them.
        -- Handles edge case of changing premake immediately after running create_parent(). 
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_prev_partition_name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||'
                ELSIF NEW.'||v_control||' >= '||quote_literal(v_prev_partition_timestamp)||' AND NEW.'||v_control||' < '||
                        quote_literal(v_prev_partition_timestamp + v_part_interval::interval)|| ' THEN 
                    INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*);';
        END IF;
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_next_partition_name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||' 
                ELSIF NEW.'||v_control||' >= '||quote_literal(v_next_partition_timestamp)||' AND NEW.'||v_control||' < '||
                    quote_literal(v_final_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*);';
        END IF;

    END LOOP;
    v_trig_func := v_trig_func ||' 
            ELSE 
                RETURN NEW; 
            END IF; 
        END IF; 
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current time interval: '||
            v_current_partition_timestamp||' to '||(v_final_partition_timestamp-'1sec'::interval));
    END IF;

ELSIF v_type = 'time-dynamic' THEN

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_count                 int;
            v_partition_name        text;
            v_partition_timestamp   timestamptz;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            ';
        CASE
            WHEN v_part_interval = '15 mins' THEN 
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''15min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 15.0);';
            WHEN v_part_interval = '30 mins' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''30min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 30.0);';
            WHEN v_part_interval = '1 hour' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||');';
             WHEN v_part_interval = '1 day' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''day'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 week' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''week'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 month' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''month'', NEW.'||v_control||');';
            WHEN v_part_interval = '3 months' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''quarter'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 year' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''year'', NEW.'||v_control||');';
        END CASE;
        v_trig_func := v_trig_func||'
            v_partition_name := @extschema@.check_name_length('''||v_parent_tablename||''', '''||v_parent_schema||''', to_char(v_partition_timestamp, '||quote_literal(v_datetime_string)||'), TRUE);
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname ||''.''|| tablename = v_partition_name;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;
        END IF;
        
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for dynamic time table: '||p_parent_table);
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid time partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


CREATE OR REPLACE FUNCTION create_trigger(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_function_name         text;
v_new_length            int;
v_parent_schema         text;
v_parent_tablename      text;
v_trig_name             text;
v_trig_sql              text;

BEGIN

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
-- Ensure function name matches the naming pattern
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);
v_trig_sql := 'CREATE TRIGGER '||v_trig_name||' BEFORE INSERT ON '||p_parent_table||
    ' FOR EACH ROW EXECUTE PROCEDURE '||v_function_name||'()';

EXECUTE v_trig_sql;

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

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
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

PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));

EXCEPTION
    WHEN QUERY_CANCELED THEN
        PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN RUN MAINTENANCE');
                v_step_id := add_step(v_job_id, 'EXCEPTION before job logging started');
            END IF;
            IF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Create the next partition in sequence for a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_next_time_partition (p_parent_table text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition            text;
v_next_partition_timestamp  timestamp;
v_next_year                 text;
v_part_interval             interval;
v_quarter                   text;
v_tablename                 text;
v_time_position             int;
v_type                      text;
v_year                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
    , last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string, v_last_partition;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

-- Double check that last created partition exists
IF v_last_partition IS NOT NULL THEN
    SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = v_last_partition;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'ERROR: previous partition table missing. Unable to determine next proper partition in sequence.';
    END IF;
ELSE
    RAISE EXCEPTION 'ERROR: last known partition missing from config table for parent table %.', p_parent_table;
END IF;

-- pull out datetime portion of last partition's tablename to make the next one
v_time_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
IF v_part_interval != '3 months' THEN
    v_next_partition_timestamp := to_timestamp(substring(v_last_partition from v_time_position), v_datetime_string) + v_part_interval;
ELSE
    -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
    v_year := split_part(substring(v_last_partition from v_time_position), 'q', 1);
    v_next_year := extract('year' from to_date(v_year, 'YYYY')+'1year'::interval); 
    v_quarter := split_part(substring(v_last_partition from v_time_position), 'q', 2);
    CASE
        WHEN v_quarter = '1' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
        WHEN v_quarter = '2' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
        WHEN v_quarter = '3' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        WHEN v_quarter = '4' THEN
            v_next_partition_timestamp := to_timestamp(v_next_year || '-01-01', 'YYYY-MM-DD');
    END CASE;
END IF;

EXECUTE 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','||quote_literal(v_part_interval)||','
    ||quote_literal(v_datetime_string)||','||quote_literal(ARRAY[v_next_partition_timestamp])||')' INTO v_last_partition; 

IF v_last_partition IS NOT NULL THEN
    UPDATE @extschema@.part_config SET last_partition = v_last_partition WHERE parent_table = p_parent_table;
END IF;

END
$$;



/*
 * Function to undo time-based partitioning created by this extension
 */
CREATE OR REPLACE FUNCTION undo_partition_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_min             timestamptz;
v_child_loop_total      bigint := 0;
v_child_table           text;
v_control               text;
v_function_name         text;
v_inner_loop_count      int;
v_job_id                bigint;
v_jobmon_schema         text;
v_move_sql              text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_part_interval         interval;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_total                 bigint := 0;
v_trig_name             text;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman undo_time_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_time_partition already running.';
    RETURN 0;
END IF;

SELECT part_interval::interval
    , control
INTO v_part_interval
    , v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'time-static' OR type = 'time-dynamic');

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_part_interval;
END IF;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables.
SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_trig_name||' ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||v_function_name||'()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

<<outer_child_loop>>
WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT n.nspname||'.'||c.relname INTO v_child_table
    FROM pg_inherits i 
    JOIN pg_class c ON i.inhrelid = c.oid 
    JOIN pg_namespace n ON c.relnamespace = n.oid 
    WHERE i.inhparent::regclass = p_parent_table::regclass 
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;

    EXECUTE 'SELECT min('||v_control||') FROM '||v_child_table INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF p_keep_table = false THEN
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        END IF;
        v_undo_count := v_undo_count + 1;
        CONTINUE outer_child_loop;
    END IF;
    v_inner_loop_count := 1;
    v_child_loop_total := 0;
    <<inner_child_loop>>
    LOOP
        -- Get everything from the current child minimum up to the multiples of the given interval
        v_move_sql := 'WITH move_data AS (DELETE FROM '||v_child_table||
                ' WHERE '||v_control||' <= '||quote_literal(v_child_min + (p_batch_interval * v_inner_loop_count))||' RETURNING *)
            INSERT INTO '||p_parent_table||' SELECT * FROM move_data';
        EXECUTE v_move_sql;
        GET DIAGNOSTICS v_rowcount = ROW_COUNT;
        v_total := v_total + v_rowcount;
        v_child_loop_total := v_child_loop_total + v_rowcount;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Moved '||v_child_loop_total||' rows to parent.');
        END IF;
        EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
        v_inner_loop_count := v_inner_loop_count + 1;
        v_batch_loop_count := v_batch_loop_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
    END LOOP inner_child_loop;
END LOOP outer_child_loop;

IF v_batch_loop_count < p_batch_count THEN
    -- FOR loop never ran, so there's no child tables left.
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) to the parent. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', 'Copied '||v_total||' row(s) to the parent. Removed '||v_undo_count||' partitions.');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman undo_time_partition'));

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to undo id-based partitioning created by this extension
 */
CREATE OR REPLACE FUNCTION undo_partition_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval bigint DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_loop_total      bigint := 0;
v_child_min             bigint;
v_child_table           text;
v_control               text;
v_function_name         text;
v_inner_loop_count      int;
v_job_id                bigint;
v_jobmon_schema         text;
v_move_sql              text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_part_interval         bigint;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_trig_name             text;
v_total                 bigint := 0;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman undo_id_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_id_partition already running.';
    RETURN 0;
END IF;

SELECT part_interval::bigint
    , control
INTO v_part_interval
    , v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'id-static' OR type = 'id-dynamic');

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_part_interval;
END IF;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_trig_name||' ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||v_function_name||'()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

<<outer_child_loop>>
WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT n.nspname||'.'||c.relname INTO v_child_table
    FROM pg_inherits i 
    JOIN pg_class c ON i.inhrelid = c.oid 
    JOIN pg_namespace n ON c.relnamespace = n.oid 
    WHERE i.inhparent::regclass = p_parent_table::regclass 
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;

    EXECUTE 'SELECT min('||v_control||') FROM '||v_child_table INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF p_keep_table = false THEN
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        END IF;
        v_undo_count := v_undo_count + 1;
        CONTINUE outer_child_loop;
    END IF;
    v_inner_loop_count := 1;
    v_child_loop_total := 0;
    <<inner_child_loop>>
    LOOP
        -- Get everything from the current child minimum up to the multiples of the given interval
        v_move_sql := 'WITH move_data AS (DELETE FROM '||v_child_table||
                ' WHERE '||v_control||' <= '||quote_literal(v_child_min + (p_batch_interval * v_inner_loop_count))||' RETURNING *)
            INSERT INTO '||p_parent_table||' SELECT * FROM move_data';
        EXECUTE v_move_sql;
        GET DIAGNOSTICS v_rowcount = ROW_COUNT;
        v_total := v_total + v_rowcount;
        v_child_loop_total := v_child_loop_total + v_rowcount;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Moved '||v_child_loop_total||' rows to parent.');
        END IF;
        EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
        v_inner_loop_count := v_inner_loop_count + 1;
        v_batch_loop_count := v_batch_loop_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
    END LOOP inner_child_loop;
END LOOP outer_child_loop;

IF v_batch_loop_count < p_batch_count THEN
    -- FOR loop never ran, so there's no child tables left.
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) to the parent. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', 'Copied '||v_total||' row(s) to the parent. Removed '||v_undo_count||' partitions.');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman undo_id_partition'));

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to undo partitioning. 
 * Will actually work on any parent/child table set, not just ones created by pg_partman.
 */
CREATE OR REPLACE FUNCTION undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      bigint := 0;
v_child_count           bigint;
v_child_table           text;
v_copy_sql              text;
v_function_name         text;
v_job_id                bigint;
v_jobmon_schema         text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_part_interval         interval;
v_rowcount              bigint;
v_step_id               bigint;
v_total                 bigint := 0;
v_trig_name             text;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman undo_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition already running.';
    RETURN 0;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_trig_name||' ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||v_function_name||'()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT n.nspname||'.'||c.relname INTO v_child_table
    FROM pg_inherits i 
    JOIN pg_class c ON i.inhrelid = c.oid 
    JOIN pg_namespace n ON c.relnamespace = n.oid 
    WHERE i.inhparent::regclass = p_parent_table::regclass 
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    EXECUTE 'SELECT count(*) FROM '||v_child_table INTO v_child_count;
    IF v_child_count = 0 THEN
        -- No rows left in this child table. Remove from partition set.
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF p_keep_table = false THEN
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||coalesce(v_rowcount, 0)||' rows to parent');
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Copied '||coalesce(v_rowcount, 0)||' rows to parent');
            END IF;
        END IF;
        v_undo_count := v_undo_count + 1;
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;
   
    v_copy_sql := 'INSERT INTO '||p_parent_table||' SELECT * FROM '||v_child_table;
    EXECUTE v_copy_sql;
    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total := v_total + v_rowcount;

    EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
    IF p_keep_table = false THEN
        EXECUTE 'DROP TABLE '||v_child_table;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||v_rowcount||' rows to parent');
        END IF;
    ELSE
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Copied '||v_rowcount||' rows to parent');
        END IF;
    END IF;
    v_batch_loop_count := v_batch_loop_count + 1;
    v_undo_count := v_undo_count + 1;         
END LOOP;

IF v_undo_count = 0 THEN
    -- FOR loop never ran, so there's no child tables left.
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman (if it existed)');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) from % child table(s) to the parent: %', v_total, v_undo_count, p_parent_table;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', 'Copied '||v_total||' row(s) from '||v_undo_count||' child table(s) to the parent');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman undo_partition'));

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to drop child tables from a time-based partition set. 
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */
CREATE OR REPLACE FUNCTION drop_partition_id(p_parent_table text, p_retention bigint DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                  boolean;
v_child_table               text;
v_control                   text;
v_drop_count                int := 0;
v_id_position               int;
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
v_retention_schema          text;
v_step_id                   bigint;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman drop_partition_id'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_id already running.';
    RETURN 0;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;


-- Allow override of configuration options
IF p_retention IS NULL THEN
    SELECT  
        part_interval::bigint
        , control
        , retention::bigint
        , retention_keep_table
        , retention_keep_index
        , retention_schema
    INTO
        v_part_interval
        , v_control
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (type = 'id-static' OR type = 'id-dynamic') 
    AND retention IS NOT NULL;

    IF v_part_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
     SELECT  
        part_interval::bigint
        , control
        , retention_keep_table
        , retention_keep_index
        , retention_schema
    INTO
        v_part_interval
        , v_control
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (type = 'id-static' OR type = 'id-dynamic'); 
    v_retention := p_retention;

    IF v_part_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN DROP ID PARTITION: '|| p_parent_table);
END IF;

EXECUTE 'SELECT max('||v_control||') FROM '||p_parent_table INTO v_max;

-- Loop through child tables of the given parent
FOR v_child_table IN 
    SELECT n.nspname||'.'||c.relname FROM pg_inherits i join pg_class c ON i.inhrelid = c.oid join pg_namespace n ON c.relnamespace = n.oid WHERE i.inhparent::regclass = p_parent_table::regclass ORDER BY i.inhrelid ASC
LOOP
    v_id_position := (length(v_child_table) - position('p_' in reverse(v_child_table))) + 2;
    v_partition_id := substring(v_child_table from v_id_position)::bigint;

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention <= (v_max - (v_partition_id + v_part_interval)) THEN
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Uninherit table '||v_child_table||' from '||p_parent_table);
        END IF;
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_schema IS NULL THEN
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
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, 'Moving table '||v_child_table||' to schema '||v_retention_schema);
            END IF;

            EXECUTE 'ALTER TABLE '||v_child_table||' SET SCHEMA '||v_retention_schema; 
            
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if
        
        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
    PERFORM update_step(v_step_id, 'OK', v_drop_count||' partitions dropped.');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman drop_partition_id'));

RETURN v_drop_count;

EXCEPTION
    WHEN QUERY_CANCELED THEN
        PERFORM pg_advisory_unlock(hashtext('pg_partman drop_partition_id'));
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
        PERFORM pg_advisory_unlock(hashtext('pg_partman drop_partition_id'));
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to drop child tables from a time-based partition set.
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */
CREATE OR REPLACE FUNCTION drop_partition_time(p_parent_table text, p_retention interval DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                  boolean;
v_child_table               text;
v_datetime_string           text;
v_drop_count                int := 0;
v_index                     record;
v_job_id                    bigint;
v_jobmon_schema             text;
v_old_search_path           text;
v_part_interval             interval;
v_partition_timestamp       timestamp;
v_quarter                   text;
v_retention                 interval;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_step_id                   bigint;
v_time_position             int;
v_year                      text;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman drop_partition_time'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_time already running.';
    RETURN 0;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

-- Allow override of configuration options
IF p_retention IS NULL THEN
    SELECT  
        part_interval::interval
        , retention::interval
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
    INTO
        v_part_interval
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (type = 'time-static' OR type = 'time-dynamic') 
    AND retention IS NOT NULL;
    
    IF v_part_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
    SELECT  
        part_interval::interval
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
    INTO
        v_part_interval
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (type = 'time-static' OR type = 'time-dynamic'); 
    v_retention := p_retention;
    
    IF v_part_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN DROP TIME PARTITION: '|| p_parent_table);
END IF;

-- Loop through child tables of the given parent
FOR v_child_table IN 
    SELECT n.nspname||'.'||c.relname FROM pg_inherits i join pg_class c ON i.inhrelid = c.oid join pg_namespace n ON c.relnamespace = n.oid WHERE i.inhparent::regclass = p_parent_table::regclass ORDER BY i.inhrelid ASC
LOOP
    -- pull out datetime portion of last partition's tablename to make the next one
    v_time_position := (length(v_child_table) - position('p_' in reverse(v_child_table))) + 2;
    IF v_part_interval != '3 months' THEN
        v_partition_timestamp := to_timestamp(substring(v_child_table from v_time_position), v_datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(substring(v_child_table from v_time_position), 'q', 1);
        v_quarter := split_part(substring(v_child_table from v_time_position), 'q', 2);
        CASE
            WHEN v_quarter = '1' THEN
                v_partition_timestamp := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
            WHEN v_quarter = '2' THEN
                v_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
            WHEN v_quarter = '3' THEN
                v_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
            WHEN v_quarter = '4' THEN
                v_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        END CASE;
    END IF;

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention < (CURRENT_TIMESTAMP - (v_partition_timestamp + v_part_interval)) THEN
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Uninherit table '||v_child_table||' from '||p_parent_table);
        END IF;
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_schema IS NULL THEN
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
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, 'Moving table '||v_child_table||' to schema '||v_retention_schema);
            END IF;

            EXECUTE 'ALTER TABLE '||v_child_table||' SET SCHEMA '||v_retention_schema; 
            
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if
        
        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
    PERFORM update_step(v_step_id, 'OK', v_drop_count||' partitions dropped.');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman drop_partition_time'));

RETURN v_drop_count;

EXCEPTION
    WHEN QUERY_CANCELED THEN
        PERFORM pg_advisory_unlock(hashtext('pg_partman drop_partition_time'));
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN DROP TIME PARTITION');
                v_step_id := add_step(v_job_id, 'EXCEPTION before job logging started');
            END IF;
            IF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        PERFORM pg_advisory_unlock(hashtext('pg_partman drop_partition_time'));
        RAISE EXCEPTION '%', SQLERRM;
END
$$;
