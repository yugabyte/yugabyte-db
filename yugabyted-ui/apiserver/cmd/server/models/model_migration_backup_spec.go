package models

// MigrationBackupSpec - Backup spec
type MigrationBackupSpec struct {

	// The UUID of the cluster
	ClusterId string `json:"cluster_id"`

	// Time to retain the backup
	RetentionPeriodInDays int32 `json:"retention_period_in_days"`

	// Description for the backup
	Description string `json:"description"`
}
