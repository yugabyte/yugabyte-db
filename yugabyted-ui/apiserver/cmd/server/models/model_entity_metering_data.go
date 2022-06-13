package models

// EntityMeteringData - Entity metering data
type EntityMeteringData struct {

	ClusterId string `json:"cluster_id"`

	ClusterEvents []ClusterMeteringEvent `json:"cluster_events"`

	BackupEvents []BackupMeteringEvent `json:"backup_events"`

	NetworkData NetworkData `json:"network_data"`
}
