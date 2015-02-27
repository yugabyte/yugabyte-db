CREATE TABLE part_config (
    parent_table text NOT NULL,
    control text NOT NULL,
    type text NOT NULL,
    part_interval text NOT NULL,
    constraint_cols text[],
    premake int NOT NULL DEFAULT 4,
    inherit_fk boolean NOT NULL DEFAULT true,
    retention text,
    retention_schema text,
    retention_keep_table boolean NOT NULL DEFAULT true,
    retention_keep_index boolean NOT NULL DEFAULT true,
    datetime_string text,
    use_run_maintenance BOOLEAN NOT NULL DEFAULT true,
    jobmon boolean NOT NULL DEFAULT true,
    undo_in_progress boolean NOT NULL DEFAULT false,
    CONSTRAINT part_config_parent_table_pkey PRIMARY KEY (parent_table),
    CONSTRAINT positive_premake_check CHECK (premake > 0)
);
CREATE INDEX part_config_type_idx ON @extschema@.part_config (type);
SELECT pg_catalog.pg_extension_config_dump('part_config', '');


-- FK set deferrable because create_parent() inserts to this table before part_config
CREATE TABLE part_config_sub (
    sub_parent text PRIMARY KEY REFERENCES @extschema@.part_config (parent_table) ON DELETE CASCADE ON UPDATE CASCADE DEFERRABLE INITIALLY DEFERRED
    , sub_type text NOT NULL
    , sub_control text NOT NULL
    , sub_part_interval text NOT NULL
    , sub_constraint_cols text[]
    , sub_premake int NOT NULL DEFAULT 4
    , sub_inherit_fk boolean NOT NULL DEFAULT true
    , sub_retention text
    , sub_retention_schema text
    , sub_retention_keep_table boolean NOT NULL DEFAULT true
    , sub_retention_keep_index boolean NOT NULL DEFAULT true
    , sub_use_run_maintenance BOOLEAN NOT NULL DEFAULT true
    , sub_jobmon boolean NOT NULL DEFAULT true
);

-- Put constraint functions & definitions here because having them separate makes the ordering of their creation harder to control. Some require the above tables to exist first.

/*
 * Check function for config table partition types
 */
CREATE FUNCTION check_partition_type (p_type text) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE SECURITY DEFINER
    AS $$
DECLARE
v_result    boolean;
BEGIN
    SELECT p_type IN ('time-static', 'time-dynamic', 'time-custom', 'id-static', 'id-dynamic') INTO v_result;
    RETURN v_result;
END
$$;

ALTER TABLE @extschema@.part_config
ADD CONSTRAINT part_config_type_check 
CHECK (@extschema@.check_partition_type(type));

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT part_config_sub_type_check
CHECK (@extschema@.check_partition_type(sub_type));

/* 
 * Ensure that sub-partitioned tables that are themselves sub-partitions have the same configuration options set when they are part of the same inheritance tree
 */
CREATE FUNCTION check_subpart_sameconfig(text) RETURNS boolean
    LANGUAGE sql STABLE
    AS $$
    WITH child_tables AS (
        SELECT n.nspname||'.'||c.relname AS tablename
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE h.inhparent::regclass = $1::regclass
    )
    SELECT CASE 
        WHEN count(*) <= 1 THEN
            true
        WHEN count(*) > 1 THEN
           false
       END
    FROM (
        SELECT DISTINCT sub_type
            , sub_control
            , sub_part_interval
            , sub_constraint_cols
            , sub_premake
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_jobmon
        FROM @extschema@.part_config_sub a
        JOIN child_tables b on a.sub_parent = b.tablename) x;
$$;

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT subpart_sameconfig_chk
CHECK (check_subpart_sameconfig(sub_parent));


