package models

// XClusterOutboundGroup - Schema for xCluster replication group details.
type XClusterOutboundGroup struct {

    // Unique identifier for the replication group.
    ReplicationGroupId string `json:"replication_group_id"`

    // The current status of the replication group (e.g., 'active', 'inactive').
    State string `json:"state"`

    // The list of IP addresses of the nodes involved in the replication.
    ReplicationClusterNodeIps []string `json:"replication_cluster_node_ips"`

    // UUID of the source universe for the replication group.
    SourceUniverseUuid string `json:"source_universe_uuid"`

    // UUID of the target universe for the replication group.
    TargetUniverseUuid string `json:"target_universe_uuid"`

    // List of namespaces from outbound and lag of each table
    TablesListWithLag []TableReplicationLagDetailsOutbound `json:"tables_list_with_lag"`
}
