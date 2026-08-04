CREATE FUNCTION @extschema@.dump_partitioned_table_definition(
  p_parent_table TEXT,
  p_ignore_template_table BOOLEAN DEFAULT false
) RETURNS TEXT
  LANGUAGE PLPGSQL STABLE
AS $$
DECLARE
  v_create_parent_definition TEXT;
  v_update_part_config_definition TEXT;
  -- Columns from part_config table.
  v_parent_table TEXT; -- NOT NULL
  v_control TEXT; -- NOT NULL
  v_partition_type TEXT; -- NOT NULL
  v_partition_interval TEXT; -- NOT NULL
  v_constraint_cols TEXT[];
  v_premake integer; -- NOT NULL
  v_optimize_trigger integer; -- NOT NULL
  v_optimize_constraint integer; -- NOT NULL
  v_epoch text; -- NOT NULL
  v_inherit_fk BOOLEAN; -- NOT NULL
  v_retention TEXT;
  v_retention_schema TEXT;
  v_retention_keep_table BOOLEAN; -- NOT NULL
  v_retention_keep_index BOOLEAN; -- NOT NULL
  v_infinite_time_partitions BOOLEAN; -- NOT NULL
  v_datetime_string TEXT;
  v_automatic_maintenance TEXT; -- NOT NULL
  v_jobmon BOOLEAN; -- NOT NULL
  v_sub_partition_set_full BOOLEAN; -- NOT NULL
  v_trigger_exception_handling BOOLEAN;
  v_upsert TEXT; -- NOT NULL
  v_trigger_return_null BOOLEAN; -- NOT NULL
  v_template_table TEXT;
  v_publications TEXT[];
  v_inherit_privileges BOOLEAN; -- DEFAULT false
  v_constraint_valid BOOLEAN; -- DEFAULT true NOT NULL
  v_subscription_refresh text; 
  v_drop_cascade_fk boolean; -- DEFAULT false NOT NULL
  v_ignore_default_data boolean; -- DEFAULT false NOT NULL
BEGIN
  SELECT
    pc.parent_table,
    pc.control,
    pc.partition_type,
    pc.partition_interval,
    pc.constraint_cols,
    pc.premake,
    pc.optimize_trigger,
    pc.optimize_constraint,
    pc.epoch,
    pc.inherit_fk,
    pc.retention,
    pc.retention_schema,
    pc.retention_keep_table,
    pc.retention_keep_index,
    pc.infinite_time_partitions,
    pc.datetime_string,
    pc.automatic_maintenance,
    pc.jobmon,
    pc.sub_partition_set_full,
    pc.trigger_exception_handling,
    pc.upsert,
    pc.trigger_return_null,
    pc.template_table,
    pc.publications,
    pc.inherit_privileges,
    pc.constraint_valid, 
    pc.subscription_refresh,
    pc.drop_cascade_fk,
    pc.ignore_default_data 
  INTO
    v_parent_table,
    v_control,
    v_partition_type,
    v_partition_interval,
    v_constraint_cols,
    v_premake,
    v_optimize_trigger,
    v_optimize_constraint,
    v_epoch,
    v_inherit_fk,
    v_retention,
    v_retention_schema,
    v_retention_keep_table,
    v_retention_keep_index,
    v_infinite_time_partitions,
    v_datetime_string,
    v_automatic_maintenance,
    v_jobmon,
    v_sub_partition_set_full,
    v_trigger_exception_handling,
    v_upsert,
    v_trigger_return_null,
    v_template_table,
    v_publications,
    v_inherit_privileges,
    v_constraint_valid,
    v_subscription_refresh,
    v_drop_cascade_fk,
    v_ignore_default_data 
  FROM @extschema@.part_config pc
  WHERE pc.parent_table = p_parent_table;

  IF v_partition_type = 'partman' THEN
    CASE
      WHEN v_partition_interval::INTERVAL = '1 year'::INTERVAL THEN
        v_partition_interval := 'yearly';
      WHEN v_partition_interval::INTERVAL = '3 months'::INTERVAL THEN
        v_partition_interval := 'quarterly';
      WHEN v_partition_interval::INTERVAL = '1 month'::INTERVAL THEN
        v_partition_interval := 'monthly';
      WHEN v_partition_interval::INTERVAL = '1 week'::INTERVAL THEN
        v_partition_interval := 'weekly';
      WHEN v_partition_interval::INTERVAL = '1 day'::INTERVAL THEN
        v_partition_interval := 'daily';
      WHEN v_partition_interval::INTERVAL = '1 hour'::INTERVAL THEN
        v_partition_interval := 'hourly';
      WHEN v_partition_interval::INTERVAL = '30 mins'::INTERVAL THEN
        v_partition_interval := 'half-hour';
      WHEN v_partition_interval::INTERVAL = '15 mins'::INTERVAL THEN
        v_partition_interval := 'quarter-hour';
      ELSE
        RAISE EXCEPTION 'Partitioning interval not recognized for "partman" partitioning type';
    END CASE;
  END IF;

  IF v_partition_type = 'native' AND p_ignore_template_table THEN
    v_template_table := NULL;
  END IF;

  v_create_parent_definition := format(
E'SELECT @extschema@.create_parent(
\tp_parent_table := %L,
\tp_control := %L,
\tp_type := %L,
\tp_interval := %L,
\tp_constraint_cols := %L,
\tp_premake := %s,
\tp_automatic_maintenance := %L,
\tp_inherit_fk := %L,
\tp_epoch := %L,
\tp_upsert := %L,
\tp_publications := %L,
\tp_trigger_return_null := %L,
\tp_template_table := %L,
\tp_jobmon := %L
\t-- v_start_partition is intentionally ignored as there
\t-- isn''t any obviously correct definition.
);',
      v_parent_table,
      v_control,
      v_partition_type,
      v_partition_interval,
      v_constraint_cols,
      v_premake,
      v_automatic_maintenance,
      v_inherit_fk,
      v_epoch,
      v_upsert,
      v_publications,
      v_trigger_return_null,
      v_template_table,
      v_jobmon
    );

  v_update_part_config_definition := format(
E'UPDATE @extschema@.part_config SET
\toptimize_trigger = %s,
\toptimize_constraint = %s,
\tretention = %L,
\tretention_schema = %L,
\tretention_keep_table = %L,
\tretention_keep_index = %L,
\tinfinite_time_partitions = %L,
\tdatetime_string = %L,
\tsub_partition_set_full = %L,
\ttrigger_exception_handling = %L,
\tinherit_privileges = %L,
\tconstraint_valid = %L,
\tsubscription_refresh = %L,
\tignore_default_data = %L
WHERE parent_table = %L;',
    v_optimize_trigger,
    v_optimize_constraint,
    v_retention,
    v_retention_schema,
    v_retention_keep_table,
    v_retention_keep_index,
    v_infinite_time_partitions,
    v_datetime_string,
    v_sub_partition_set_full,
    v_trigger_exception_handling,
    v_inherit_privileges,
    v_constraint_valid,
    v_subscription_refresh,
    v_ignore_default_data,
    v_parent_table
  );

  RETURN concat_ws(E'\n',
    v_create_parent_definition,
    v_update_part_config_definition
  );
END
$$;

