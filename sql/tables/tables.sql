CREATE TABLE part_config (
    parent_table text NOT NULL
    , control text NOT NULL
    , partition_type text NOT NULL
    , partition_interval text NOT NULL
    , constraint_cols text[]
    , premake int NOT NULL DEFAULT 4
    , optimize_trigger int NOT NULL DEFAULT 4
    , optimize_constraint int NOT NULL DEFAULT 30
    , epoch text NOT NULL DEFAULT 'none' 
    , inherit_fk boolean NOT NULL DEFAULT true
    , retention text
    , retention_schema text
    , retention_keep_table boolean NOT NULL DEFAULT true
    , retention_keep_index boolean NOT NULL DEFAULT true
    , infinite_time_partitions boolean NOT NULL DEFAULT false
    , datetime_string text
    , use_run_maintenance BOOLEAN NOT NULL DEFAULT true
    , jobmon boolean NOT NULL DEFAULT true
    , sub_partition_set_full boolean NOT NULL DEFAULT false
    , undo_in_progress boolean NOT NULL DEFAULT false
    , trigger_exception_handling BOOLEAN DEFAULT false
    , upsert text NOT NULL DEFAULT ''
    , trigger_return_null boolean NOT NULL DEFAULT true
    , CONSTRAINT part_config_parent_table_pkey PRIMARY KEY (parent_table)
    , CONSTRAINT positive_premake_check CHECK (premake > 0)
);
CREATE INDEX part_config_type_idx ON @extschema@.part_config (partition_type);
SELECT pg_catalog.pg_extension_config_dump('part_config', '');


-- FK set deferrable because create_parent() inserts to this table before part_config
CREATE TABLE part_config_sub (
    sub_parent text 
    , sub_partition_type text NOT NULL
    , sub_control text NOT NULL
    , sub_partition_interval text NOT NULL
    , sub_constraint_cols text[]
    , sub_premake int NOT NULL DEFAULT 4
    , sub_optimize_trigger int NOT NULL DEFAULT 4
    , sub_optimize_constraint int NOT NULL DEFAULT 30
    , sub_epoch text NOT NULL DEFAULT 'none' 
    , sub_inherit_fk boolean NOT NULL DEFAULT true
    , sub_retention text
    , sub_retention_schema text
    , sub_retention_keep_table boolean NOT NULL DEFAULT true
    , sub_retention_keep_index boolean NOT NULL DEFAULT true
    , sub_infinite_time_partitions boolean NOT NULL DEFAULT false
    , sub_use_run_maintenance BOOLEAN NOT NULL DEFAULT true
    , sub_jobmon boolean NOT NULL DEFAULT true
    , sub_trigger_exception_handling BOOLEAN DEFAULT false
    , sub_upsert TEXT NOT NULL DEFAULT ''
    , sub_trigger_return_null boolean NOT NULL DEFAULT true
    , CONSTRAINT part_config_sub_pkey PRIMARY KEY (sub_parent)
    , CONSTRAINT part_config_sub_sub_parent_fkey FOREIGN KEY (sub_parent) REFERENCES @extschema@.part_config (parent_table) ON DELETE CASCADE ON UPDATE CASCADE DEFERRABLE INITIALLY DEFERRED
    , CONSTRAINT positive_premake_check CHECK (sub_premake > 0)
);
SELECT pg_catalog.pg_extension_config_dump('part_config_sub', '');

CREATE TABLE custom_time_partitions (
    parent_table text NOT NULL
    , child_table text NOT NULL
    , partition_range tstzrange NOT NULL
    , PRIMARY KEY (parent_table, child_table));
CREATE INDEX custom_time_partitions_partition_range_idx ON custom_time_partitions USING gist (partition_range);
SELECT pg_catalog.pg_extension_config_dump('custom_time_partitions', '');


-- Put constraint functions & definitions here because having them separate makes the ordering of their creation harder to control. Some require the above tables to exist first.
/*
 * Check function for config table partition types
 */
CREATE FUNCTION @extschema@.check_partition_type (p_type text) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE SECURITY DEFINER
    AS $$
DECLARE
v_result    boolean;
BEGIN
    SELECT p_type IN ('time', 'time-custom', 'id') INTO v_result;
    RETURN v_result;
END
$$;

ALTER TABLE @extschema@.part_config
ADD CONSTRAINT part_config_type_check 
CHECK (@extschema@.check_partition_type(partition_type));

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT part_config_sub_type_check
CHECK (@extschema@.check_partition_type(sub_partition_type));

/*
 * Check function for config table epoch types
 */
CREATE FUNCTION @extschema@.check_epoch_type (p_type text) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE SECURITY DEFINER
    AS $$
DECLARE
v_result    boolean;
BEGIN
    SELECT p_type IN ('none', 'seconds', 'milliseconds') INTO v_result;
    RETURN v_result;
END
$$;

ALTER TABLE @extschema@.part_config
ADD CONSTRAINT part_config_epoch_check 
CHECK (@extschema@.check_epoch_type(epoch));

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT part_config_sub_epoch_check 
CHECK (@extschema@.check_epoch_type(sub_epoch));


-- Ensure the control column cannot be one of the additional constraint columns. 
ALTER TABLE @extschema@.part_config ADD CONSTRAINT control_constraint_col_chk CHECK ((constraint_cols @> ARRAY[control]) <> true);
ALTER TABLE @extschema@.part_config_sub ADD CONSTRAINT control_constraint_col_chk CHECK ((sub_constraint_cols @> ARRAY[sub_control]) <> true);


/*
 * Custom view to help improve privilege lookups for pg_partman. 
 * information_schema is a performance bottleneck since indexes aren't being used properly.
 */
CREATE VIEW @extschema@.table_privs AS
    SELECT u_grantor.rolname AS grantor,
           grantee.rolname AS grantee,
           nc.nspname AS table_schema,
           c.relname AS table_name,
           c.prtype AS privilege_type
    FROM (
            SELECT oid, relname, relnamespace, relkind, relowner, (aclexplode(coalesce(relacl, acldefault('r', relowner)))).* FROM pg_class
         ) AS c (oid, relname, relnamespace, relkind, relowner, grantor, grantee, prtype, grantable),
         pg_namespace nc,
         pg_authid u_grantor,
         (
           SELECT oid, rolname FROM pg_authid
           UNION ALL
           SELECT 0::oid, 'PUBLIC'
         ) AS grantee (oid, rolname)
    WHERE c.relnamespace = nc.oid
          AND c.relkind IN ('r', 'v')
          AND c.grantee = grantee.oid
          AND c.grantor = u_grantor.oid
          AND c.prtype IN ('INSERT', 'SELECT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER')
          AND (pg_has_role(u_grantor.oid, 'USAGE')
               OR pg_has_role(grantee.oid, 'USAGE')
               OR grantee.rolname = 'PUBLIC' );


