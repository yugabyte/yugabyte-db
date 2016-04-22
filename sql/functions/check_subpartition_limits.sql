/*
 * Check if parent table is a subpartition of an already existing partition set managed by pg_partman
 *  If so, return the limits of what child tables can be created under the given parent table based on its own suffix
 */
CREATE FUNCTION check_subpartition_limits(p_parent_table text, p_type text, OUT sub_min text, OUT sub_max text) RETURNS record
    LANGUAGE plpgsql
    AS $$
DECLARE

v_datetime_string       text;
v_id_position           int;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_interval    interval;
v_quarter               text;
v_sub_id_max            bigint;
v_sub_id_min            bigint;
v_sub_timestamp_max     timestamp;
v_sub_timestamp_min     timestamp;
v_time_position         int;
v_top_datetime_string   text;
v_top_interval          text;
v_top_parent            text;
v_top_type              text;
v_year                  text;

BEGIN

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

-- CTE query is done individually for each type (time, id) because it should return NULL if the top parent is not the same type in a subpartition set (id->time or time->id)

IF p_type = 'id' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_parent_tablename
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.partition_interval, p.partition_type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.partition_type = 'id';

    IF v_top_parent IS NOT NULL THEN 
        SELECT child_start_id::text, child_end_id::text 
        INTO sub_min, sub_max
        FROM @extschema@.show_partition_info(p_parent_table, v_top_interval, v_top_parent);
    END IF;

ELSIF p_type = 'time' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_parent_tablename
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.partition_interval, p.partition_type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.partition_type = 'time' OR p.partition_type = 'time-custom';

    IF v_top_parent IS NOT NULL THEN 
        SELECT child_start_time::text, child_end_time::text 
        INTO sub_min, sub_max
        FROM @extschema@.show_partition_info(p_parent_table, v_top_interval, v_top_parent);
    END IF;

ELSE
    RAISE EXCEPTION 'Invalid type given as parameter to check_subpartition_limits()';
END IF;

RETURN;

END
$$;

