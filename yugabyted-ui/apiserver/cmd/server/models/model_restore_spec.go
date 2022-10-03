package models

// RestoreSpec - Backup spec
type RestoreSpec struct {

	// The UUID of the backup
	BackupId string `json:"backup_id"`

	// The UUID of the cluster
	ClusterId string `json:"cluster_id"`
}
