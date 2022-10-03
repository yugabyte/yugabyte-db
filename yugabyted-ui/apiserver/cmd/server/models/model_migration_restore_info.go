package models

// MigrationRestoreInfo - Migration Restore Info
type MigrationRestoreInfo struct {

	// The UUID of the restore
	Id string `json:"id"`

	// The name of the cluster to which backup is being restored
	ClusterName string `json:"cluster_name"`

	// Description of the restore operation
	Description string `json:"description"`

	State RestoreStateEnum `json:"state"`

	Metadata EntityMetadata `json:"metadata"`
}
