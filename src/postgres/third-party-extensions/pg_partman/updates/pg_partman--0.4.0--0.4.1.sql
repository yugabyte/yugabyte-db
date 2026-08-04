-- Changed the privilege management system to apply the current parent's privileges only to new child tables at the time they're created. No longer re-applies privileges to existing child tables. When partition sets grew large, this was causing serious performance problems and was too expensive an operation to run every time a child was created.
-- Dropped apply_grants() function. New child table privileges are now managed by the partition creation functions themselves.
-- Created reapply_privileges() function to reset the privileges of all child tables in a given partition set. Uses the given parent's privileges at the time the function is run. All new grants will be set and all that don't exist will be revoked. Ownership will be updated if it has changed.
-- First round of pgTAP tests.

DROP FUNCTION apply_grants(text);
DROP TYPE IF EXISTS @extschema@.partition_type;

/*
 * Function to re-apply ownership & privileges on all child tables in a partition set using parent table as reference
 */
CREATE FUNCTION reapply_privileges(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_owner       text;
v_child_table       text;
v_child_grant       record;
v_count             int;
v_grant             text;
v_grantees          text[];
v_job_id            bigint;
v_jobmon_schema     text;
v_match             boolean;
v_old_search_path   text;
v_parent_owner      text;
v_owner_sql         text;
v_revoke            text[];
v_parent_grant      record;
v_sql               text;
v_step_id           bigint;

BEGIN

SELECT count(*) INTO v_count FROM @extschema@.part_config WHERE parent_table = p_parent_table;
IF v_count = 0 THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %', p_parent_table;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Setting new child table privileges');
END IF;

SELECT tableowner INTO v_parent_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOR v_child_table IN 
    SELECT inhrelid::regclass FROM pg_catalog.pg_inherits WHERE inhparent::regclass = p_parent_table::regclass ORDER BY inhrelid::regclass ASC
LOOP
    PERFORM update_step(v_step_id, 'PENDING', 'Currently on child partition in ascending order: '||v_child_table);
    v_grantees := NULL;
    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        -- Compare parent & child grants. Don't re-apply if it already exists
        v_match := false;
        FOR v_child_grant IN 
            SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
            FROM information_schema.table_privileges 
            WHERE table_schema ||'.'|| table_name = v_child_table
            GROUP BY grantee 
        LOOP
            IF v_parent_grant.types = v_child_grant.types AND v_parent_grant.grantee = v_child_grant.grantee THEN
                v_match := true;
            END IF;
        END LOOP;

        IF v_match = false THEN
            EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_child_table||' TO '||v_parent_grant.grantee;
            SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
            IF v_revoke IS NOT NULL THEN
                EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_child_table||' FROM '||v_parent_grant.grantee||' CASCADE';
            END IF;
        END IF;

        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);

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

    SELECT tableowner INTO v_child_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = v_child_table;
    IF v_parent_owner <> v_child_owner THEN
        EXECUTE 'ALTER TABLE '||v_child_table||' OWNER TO '||v_parent_owner;
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    PERFORM close_job(v_job_id);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

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

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_grantees          text[];
v_job_id            bigint;
v_jobmon_schema     text;
v_old_search_path   text;
v_parent_grant      record;
v_parent_owner      text;
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

SELECT tableowner INTO v_parent_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

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
v_partition_name                text;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text[];
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

SELECT tableowner INTO v_parent_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

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
