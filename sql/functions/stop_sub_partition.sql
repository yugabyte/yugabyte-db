CREATE FUNCTION @extschema@.stop_sub_partition(p_parent_table text, p_jobmon boolean DEFAULT true) RETURNS boolean
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


