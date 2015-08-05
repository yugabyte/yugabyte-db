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

-- CTE query is done individually for each type (time, id) because it should return NULL if the top parent is not the same type in a subpartition set (id->time or time->id)

IF p_type = 'id' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname||'.'||c.relname = p_parent_table
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.partition_interval, p.partition_type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.partition_type = 'id';

    IF v_top_parent IS NOT NULL THEN 
        v_id_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
        v_sub_id_min := substring(p_parent_table from v_id_position)::bigint;
        v_sub_id_max := (v_sub_id_min + v_top_interval::bigint) - 1;
        sub_min := v_sub_id_min::text;
        sub_max := v_sub_id_max::text;
    END IF;

ELSIF p_type = 'time' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname||'.'||c.relname = p_parent_table
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.partition_interval, p.partition_type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.partition_type = 'time' OR p.partition_type = 'time-custom';

    IF v_top_parent IS NOT NULL THEN 
        v_time_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
        IF v_top_interval::interval <> '3 months' OR (v_top_interval::interval = '3 months' AND v_top_type = 'time-custom') THEN
           v_sub_timestamp_min := to_timestamp(substring(p_parent_table from v_time_position), v_top_datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(substring(p_parent_table from v_time_position), 'q', 1);
            v_quarter := split_part(substring(p_parent_table from v_time_position), 'q', 2);
            CASE
                WHEN v_quarter = '1' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
                WHEN v_quarter = '2' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
                WHEN v_quarter = '3' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
                WHEN v_quarter = '4' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            END CASE;
        END IF;
        v_sub_timestamp_max = (v_sub_timestamp_min + v_top_interval::interval) - '1 sec'::interval;

        sub_min := v_sub_timestamp_min::text;
        sub_max := v_sub_timestamp_max::text;
    END IF;

ELSE
    RAISE EXCEPTION 'Invalid type given as parameter to check_subpartition_limits()';
END IF;

RETURN;

END
$$;
