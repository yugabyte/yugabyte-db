package models

// TableReplicationInfo - TableReplicationInfo
type TableReplicationInfo struct {

    NumReplicas int32 `json:"num_replicas"`

    PlacementBlocks []PlacementBlock `json:"placement_blocks"`

    PlacementUuid string `json:"placement_uuid"`
}
