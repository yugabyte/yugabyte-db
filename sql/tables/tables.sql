CREATE TABLE part_config (
    parent_table text NOT NULL,
    type @extschema@.partition_type NOT NULL,
    part_interval text NOT NULL,
    control text NOT NULL,
    premake int NOT NULL,
    retention int,
    datetime_string text,
    last_partition text,
    CONSTRAINT part_config_parent_table_pkey PRIMARY KEY (parent_table)
);
CREATE INDEX part_config_type_idx ON @extschema@.part_config (type);
SELECT pg_catalog.pg_extension_config_dump('part_config', '');
