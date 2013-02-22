CREATE TABLE part_config (
    parent_table text NOT NULL,
    type text NOT NULL,
    part_interval text NOT NULL,
    control text NOT NULL,
    premake int NOT NULL DEFAULT 4,
    retention text,
    retention_keep_table boolean NOT NULL DEFAULT true,
    retention_keep_index boolean NOT NULL DEFAULT true,
    datetime_string text,
    last_partition text,
    CONSTRAINT part_config_parent_table_pkey PRIMARY KEY (parent_table),
    CONSTRAINT part_config_type_check CHECK (@extschema@.check_partition_type(type)),
    CONSTRAINT positive_premake_check CHECK (premake > 0)
);
CREATE INDEX part_config_type_idx ON @extschema@.part_config (type);
SELECT pg_catalog.pg_extension_config_dump('part_config', '');


