CREATE TABLE custom_time_partitions (
    parent_table text NOT NULL
    , child_table text NOT NULL
    , partition_range tstzrange NOT NULL
    , PRIMARY KEY (parent_table, child_table));
CREATE INDEX custom_time_partitions_partition_range_idx ON custom_time_partitions USING gist (partition_range);
