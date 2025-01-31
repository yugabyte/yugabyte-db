CREATE FUNCTION @extschema@.check_subpartition_limits(p_parent_table text, p_type text, OUT sub_min text, OUT sub_max text) RETURNS record
    LANGUAGE plpgsql
    AS $$
DECLARE

v_parent_schema         text;
v_parent_tablename      text;
v_top_control           text;
v_top_control_type      text;
v_top_epoch             text;
v_top_interval          text;
v_top_schema            text;
v_top_tablename         text;

BEGIN
/*
 * Check if parent table is a subpartition of an already existing partition set managed by pg_partman
 *  If so, return the limits of what child tables can be created under the given parent table based on its own suffix
 *  If not, return NULL. Allows caller to check for NULL and then know if the given parent has sub-partition limits.
 */

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

WITH top_oid AS (
    SELECT i.inhparent AS top_parent_oid
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
    JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname = v_parent_schema
    AND c.relname = v_parent_tablename
) 
SELECT n.nspname, c.relname, p.partition_interval, p.control, p.epoch
INTO v_top_schema, v_top_tablename, v_top_interval, v_top_control, v_top_epoch
FROM pg_catalog.pg_class c
JOIN top_oid t ON c.oid = t.top_parent_oid
JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
WHERE c.oid = t.top_parent_oid;

SELECT general_type INTO v_top_control_type
FROM @extschema@.check_control_type(v_top_schema, v_top_tablename, v_top_control);

IF v_top_control_type = 'id' AND v_top_epoch <> 'none' THEN
    v_top_control_type := 'time';
END IF;

-- If sub-partition is different type than top parent, no need to set limits
IF p_type = v_top_control_type THEN
    IF p_type = 'time' THEN
        SELECT child_start_time::text, child_end_time::text 
        INTO sub_min, sub_max
        FROM @extschema@.show_partition_info(p_parent_table, v_top_interval, v_top_schema||'.'||v_top_tablename);
    ELSIF p_type = 'id' THEN
        SELECT child_start_id::text, child_end_id::text 
        INTO sub_min, sub_max
        FROM @extschema@.show_partition_info(p_parent_table, v_top_interval, v_top_schema||'.'||v_top_tablename);
    ELSE
        RAISE EXCEPTION 'Reached unknown state in check_subpartition_limits(). Please report what lead to this condition to author';
    END IF;
END IF;

RETURN;

END
$$;


