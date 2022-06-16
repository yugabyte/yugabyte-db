package models

// MigrationBackupInfo - Backup migration spec
type MigrationBackupInfo struct {

	// The UUID of the backup
	Id string `json:"id"`

	// The name of the cluster
	ClusterName string `json:"cluster_name"`

	// The UUID of the project
	ProjectId string `json:"project_id"`

	// The UUID of the platform
	PlatformId string `json:"platform_id"`

	// Region of the bucket
	Region string `json:"region"`

	// The name of the bucket
	BucketName string `json:"bucket_name"`

	// Credentials of the backup
	Credentials string `json:"credentials"`

	State BackupStateEnum `json:"state"`

	CloudType CloudEnum `json:"cloud_type"`

	// List of keyspaces and their location
	KeyspacesInfo []MigrationBackupKeyspaceInfo `json:"keyspaces_info"`
}
