package models

// XClusterTableInfoInbound - Schema for table details in xCluster replication.
type XClusterTableInfoInbound struct {

    // Unique identifier for the table.
    TableUuid string `json:"table_uuid"`

    // Unique identifier for the namespace.
    NamespaceId string `json:"namespace_id"`

    // Name of the table.
    TableName string `json:"table_name"`

    // Stream ID associated with the table for replication.
    StreamId string `json:"stream_id"`

    // The current state of the table.
    State string `json:"state"`

    // Keyspace to which the table belongs.
    Keyspace string `json:"keyspace"`

    // The average time taken by the consumer to fetch changes from the producer
    AvgGetChangesLatencyMs float32 `json:"avg_get_changes_latency_ms"`

    // The average time taken to apply received changes to the consumer table.
    AvgApplyLatencyMs float32 `json:"avg_apply_latency_ms"`

    // The average throughput in KiB per second for this replication stream.
    AvgThroughputKiBps float32 `json:"avg_throughput_KiBps"`
}
