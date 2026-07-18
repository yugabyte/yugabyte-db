package models

type XClusterNamespaceMetrics struct {

    // Unique replication group id of each replication
    ReplicationGroupId string `json:"replication_group_id"`

    NamespaceList []NamespacesInfo `json:"namespace_list"`

    SourcePlacementLocation []XClusterPlacementLocation `json:"source_placement_location"`

    TargetPlacementLocation []XClusterPlacementLocation `json:"target_placement_location"`

    SourceUniverseUuid string `json:"source_universe_uuid"`

    TargetUniverseUuid string `json:"target_universe_uuid"`
}
