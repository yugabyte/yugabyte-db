CREATE FUNCTION @extschema@.check_subpart_sameconfig(p_parent_table text) 
    RETURNS TABLE (sub_partition_type text
        , sub_control text
        , sub_partition_interval text
        , sub_constraint_cols text[]
        , sub_premake int
        , sub_optimize_trigger int
        , sub_optimize_constraint int
        , sub_epoch text
        , sub_inherit_fk boolean
        , sub_retention text
        , sub_retention_schema text
        , sub_retention_keep_table boolean
        , sub_retention_keep_index boolean
        , sub_infinite_time_partitions boolean
        , sub_automatic_maintenance text
        , sub_jobmon boolean
        , sub_trigger_exception_handling boolean
        , sub_upsert text
        , sub_trigger_return_null boolean
        , sub_template_table text
        , sub_inherit_privileges boolean
        , sub_constraint_valid boolean
        , sub_subscription_refresh text
        , sub_date_trunc_interval text
        , sub_ignore_default_data boolean)
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
    -- This column list must be kept consistent between: 
    --   create_parent, check_subpart_sameconfig, create_partition_id, create_partition_time, dump_partitioned_table_definition, and table definition
    SELECT DISTINCT a.sub_partition_type
        , a.sub_control
        , a.sub_partition_interval
        , a.sub_constraint_cols
        , a.sub_premake
        , a.sub_optimize_trigger
        , a.sub_optimize_constraint
        , a.sub_epoch
        , a.sub_inherit_fk
        , a.sub_retention
        , a.sub_retention_schema
        , a.sub_retention_keep_table
        , a.sub_retention_keep_index
        , a.sub_infinite_time_partitions
        , a.sub_automatic_maintenance
        , a.sub_jobmon
        , a.sub_trigger_exception_handling
        , a.sub_upsert
        , a.sub_trigger_return_null
        , a.sub_template_table
        , a.sub_inherit_privileges
        , a.sub_constraint_valid
        , a.sub_subscription_refresh
        , a.sub_date_trunc_interval
        , a.sub_ignore_default_data
    FROM @extschema@.part_config_sub a
    JOIN child_tables b on a.sub_parent = b.tablename;
$$;

