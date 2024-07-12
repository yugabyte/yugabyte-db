-- No separate configuration required for setting privileges on child tables anymore. Grants config table has been dropped. Please apply the grants you need to the parent table and they will be set for all children using that. Note that unlike before, privilges that don't exist on the parent will now be revoked from all child tables.
-- create_parent() now enforces that a given parent table be schema qualified. Ensures that a custom search_path doesn't affect the wrong table by accident.
-- Removed enum custom type and replace with check function.
-- Applying of grants is now logged in pg_jobmon so if there's any issues with that step, it's clear where it failed.

/*
 * Check function for config table partition types
 */
CREATE FUNCTION check_partition_type (p_type text) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE SECURITY DEFINER
    AS $$
DECLARE
v_result    boolean;
BEGIN
    SELECT p_type IN ('time-static', 'time-dynamic', 'id-static', 'id-dynamic') INTO v_result;
    RETURN v_result;
END
$$;

DROP FUNCTION @extschema@.create_parent(text, text, @extschema@.partition_type, text, int, boolean);
DROP TABLE @extschema@.part_grants;

ALTER TABLE @extschema@.part_config ADD COLUMN new_type text;
UPDATE @extschema@.part_config SET new_type = type;
ALTER TABLE @extschema@.part_config ALTER new_type SET NOT NULL;
ALTER TABLE @extschema@.part_config DROP COLUMN type;
DROP TYPE @extschema@.partition_type;
ALTER TABLE @extschema@.part_config RENAME new_type TO type;
ALTER TABLE @extschema@.part_config ADD CONSTRAINT part_config_type_check CHECK (@extschema@.check_partition_type(type));


/*
 * Function to apply ownership & privileges on child tables using parent table as reference
 */
CREATE OR REPLACE FUNCTION apply_grants(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_table   text;
v_count         int := 0;
v_grant         text;
v_grantees      text[];
v_owner         text;
v_owner_sql     text;
v_revoke        text[];
v_revoke_sql    text;
v_row           record;
v_sql           text;

BEGIN

SELECT count(parent_table) INTO v_count FROM @extschema@.part_config WHERE parent_table = p_parent_table;
IF v_count = 0  THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %', p_parent_table;
END IF;

SELECT tableowner INTO v_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOR v_child_table IN 
    SELECT inhrelid::regclass FROM pg_catalog.pg_inherits WHERE inhparent::regclass = p_parent_table::regclass ORDER BY inhrelid::regclass ASC
LOOP
    v_grantees := NULL;
    FOR v_row IN 
        SELECT array_agg(privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_row.types, ',')||' ON '||v_child_table||' TO '||v_row.grantee;

        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_row.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_child_table||' FROM '||v_row.grantee||' CASCADE';
        END IF;

        v_grantees := array_append(v_grantees, v_row.grantee::text);
    END LOOP;
    
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_child_table
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_child_table||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_child_table||' OWNER TO '||v_owner;

END LOOP;

END
$$;


/*
 * Function to turn a table into the parent of a partition set
 */
CREATE FUNCTION create_parent(p_parent_table text, p_control text, p_type text, p_interval text, p_premake int DEFAULT 4, p_debug boolean DEFAULT false) RETURNS void
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

BEGIN

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
    END IF;

SELECT attnotnull INTO v_notnull FROM pg_attribute WHERE attrelid = p_parent_table::regclass AND attname = p_control;
    IF v_notnull = false THEN
        RAISE EXCEPTION 'Control column (%) for parent table (%) must be NOT NULL', p_control, p_parent_table;
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
        ELSE
            RAISE EXCEPTION 'Invalid interval for time based partitioning: %', p_interval;
        END IF;
END CASE;

IF p_type = 'time-static' OR p_type = 'time-dynamic' THEN
    FOR i IN 0..p_premake LOOP
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
        v_partition_id = array_append(v_partition_id, (v_id_interval*i)+v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake) VALUES
        (p_parent_table, p_type, v_id_interval, p_control, p_premake);
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
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE PARENT: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition creation for table '||p_parent_table||' failed');
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
IF v_part_interval != '3 months' THEN
    v_next_partition_timestamp := to_timestamp(substring(v_last_partition from char_length(p_parent_table||'_p')+1), v_datetime_string) + v_part_interval;
ELSE
    -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
    v_year := split_part(substring(v_last_partition from char_length(p_parent_table||'_p')+1), 'q', 1);
    v_next_year := extract('year' from to_date(v_year, 'YYYY')+'1year'::interval); 
    v_quarter := split_part(substring(v_last_partition from char_length(p_parent_table||'_p')+1), 'q', 2);
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
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_partition (p_parent_table text, p_control text, p_interval interval, p_datetime_string text, p_partition_times timestamp[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_partition_name                text;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_step_id                       bigint;
v_tablename                     text;
v_time                          timestamp;
v_year                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    

    v_partition_name := p_parent_table || '_p';

    IF p_interval = '1 year' OR p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'YYYY');
        
        IF p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
            v_partition_name := v_partition_name || '_' || to_char(v_time, 'MM');

            IF p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
                v_partition_name := v_partition_name || '_' || to_char(v_time, 'DD');

                IF p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
                    v_partition_name := v_partition_name || '_' || to_char(v_time, 'HH24');
                    IF p_interval <> '30 mins' AND p_interval <> '15 mins' THEN
                        v_partition_name := v_partition_name || '00';
                    ELSIF p_interval = '15 mins' THEN
                        IF date_part('minute', v_time) < 15 THEN
                            v_partition_name := v_partition_name || '00';
                        ELSIF date_part('minute', v_time) >= 15 AND date_part('minute', v_time) < 30 THEN
                            v_partition_name := v_partition_name || '15';
                        ELSIF date_part('minute', v_time) >= 30 AND date_part('minute', v_time) < 45 THEN
                            v_partition_name := v_partition_name || '30';
                        ELSE
                            v_partition_name := v_partition_name || '45';
                        END IF;
                    ELSIF p_interval = '30 mins' THEN
                        IF date_part('minute', v_time) < 30 THEN
                            v_partition_name := v_partition_name || '00';
                        ELSE
                            v_partition_name := v_partition_name || '30';
                        END IF;
                    END IF;
                END IF; -- end hour IF      
            END IF; -- end day IF
        END IF; -- end month IF
    ELSIF p_interval = '1 week' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
    END IF; -- end year/week IF

    -- pull out datetime portion of last partition's tablename
    v_partition_timestamp_start := to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), p_datetime_string);
    v_partition_timestamp_end := to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), p_datetime_string) + p_interval;

    -- "Q" is ignored in to_timestamp, so handle special case
    IF p_interval = '3 months' THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_name := v_partition_name || v_year || 'q' || v_quarter;
        CASE 
            WHEN v_quarter = '1' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
            WHEN v_quarter = '2' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
            WHEN v_quarter = '3' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
            WHEN v_quarter = '4' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        END CASE;
        v_partition_timestamp_end := v_partition_timestamp_start + p_interval;
    END IF;

    SELECT schemaname ||'.'|| tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
    END IF;

    IF position('.' in p_parent_table) > 0 THEN 
        v_tablename := substring(v_partition_name from position('.' in v_partition_name)+1);
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING INDEXES)';
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check
        CHECK ('||p_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||p_control||'<'||quote_literal(v_partition_timestamp_end)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN APPLYING GRANTS: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Looping through all child tables applying privileges of the parent');
END IF;
PERFORM @extschema@.apply_grants(p_parent_table);
IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    PERFORM close_job(v_job_id);
END IF;

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
 * Function to create id partitions
 */
CREATE OR REPLACE FUNCTION create_id_partition (p_parent_table text, p_control text, p_interval bigint, p_partition_ids bigint[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_job_id            bigint;
v_jobmon_schema     text;
v_old_search_path   text;
v_partition_name    text;
v_step_id           bigint;
v_tablename         text;
v_id                bigint;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

FOREACH v_id IN ARRAY p_partition_ids LOOP

    v_partition_name := p_parent_table||'_p'||v_id;
        
    SELECT schemaname ||'.'|| tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;

    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + p_interval)-1);
    END IF;

    IF position('.' in p_parent_table) > 0 THEN 
        v_tablename := substring(v_partition_name from position('.' in v_partition_name)+1);
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING INDEXES)';
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
        CHECK ('||p_control||'>='||quote_literal(v_id)||' AND '||p_control||'<'||quote_literal(v_id + p_interval)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN APPLYING GRANTS: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Looping through all child tables applying privileges of the parent');
END IF;
PERFORM @extschema@.apply_grants(p_parent_table);
IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    PERFORM close_job(v_job_id);
END IF;

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
