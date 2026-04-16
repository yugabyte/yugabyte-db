package models

type TableInfoTableReplicationInfo struct {

    LiveReplicas TableReplicationInfo `json:"live_replicas"`

    ReadReplicas []TableReplicationInfo `json:"read_replicas"`
}
