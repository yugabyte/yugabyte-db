package models

// XClusterTableInfo - Schema for table details in the namespace involved in xCluster replication.
type XClusterTableInfo struct {

    // Unique identifier for the table.
    TableUuid string `json:"table_uuid"`

    // Unique identifier for the namespace.
    NamespaceId string `json:"namespace_id"`

    // Name of the table.
    TableName string `json:"table_name"`

    // Stream ID associated with the table for replication.
    StreamId string `json:"stream_id"`

    // The current state of the table (e.g., 'active', 'checkpointing').
    State string `json:"state"`

    // Keyspace to which the table belongs.
    Keyspace string `json:"keyspace"`

    // Indicates whether the table is currently checkpointing.
    IsCheckpointing bool `json:"is_checkpointing"`

    // Indicates whether the table is part of the initial bootstrap process.
    IsPartOfInitialBootstrap bool `json:"is_part_of_initial_bootstrap"`
}
