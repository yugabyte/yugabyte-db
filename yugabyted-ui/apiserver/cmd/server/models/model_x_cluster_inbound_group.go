package models

// XClusterInboundGroup - Schema for xCluster replication group details.
type XClusterInboundGroup struct {

    // Unique identifier for the replication group.
    ReplicationGroupId string `json:"replication_group_id"`

    // The current status of the replication group.
    State string `json:"state"`

    // List of target-side node IPs involved in replication.
    TargetClusterNodeIps []string `json:"target_cluster_node_ips"`

    // List of source-side node IPs involved in replication.
    SourceClusterNodeIps []string `json:"source_cluster_node_ips"`

    // UUID of the source universe for the replication group.
    SourceUniverseUuid string `json:"source_universe_uuid"`

    // UUID of the target universe for the replication group.
    TargetUniverseUuid string `json:"target_universe_uuid"`
}
