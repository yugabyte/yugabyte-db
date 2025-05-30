package models

// TableReplicationLagDetailsOutbound - Schema for table details and namespace in replication.
type TableReplicationLagDetailsOutbound struct {

    // Namespace to which the current table belongs.
    Namespace string `json:"namespace"`

    // Unique identifier for the table.
    TableUuid string `json:"table_uuid"`

    // Name of the table.
    TableName string `json:"table_name"`

    // Indicates whether the table is currently checkpointing.
    IsCheckpointing bool `json:"is_checkpointing"`

    // Indicates whether the table is part of the initial bootstrap process.
    IsPartOfInitialBootstrap bool `json:"is_part_of_initial_bootstrap"`

    // The lag time in microseconds for sent replication data.
    AsyncReplicationSentLagMicros int64 `json:"async_replication_sent_lag_micros"`

    // The lag time in microseconds for committed replication data.
    AsyncReplicationCommittedLagMicros int64 `json:"async_replication_committed_lag_micros"`
}
