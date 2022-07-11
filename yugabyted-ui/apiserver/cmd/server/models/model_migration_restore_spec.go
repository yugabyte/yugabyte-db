package models

// MigrationRestoreSpec - Backup migration spec
type MigrationRestoreSpec struct {

	CloudType CloudEnum `json:"cloud_type"`

	// Credentials of the backup bucket
	Credentials string `json:"credentials"`

	// Name of the backup bucket
	BucketName string `json:"bucket_name"`

	// Region of the bucket
	Region string `json:"region"`

	// Number of parallel operations
	NumParallelOps int32 `json:"num_parallel_ops"`

	// List of keyspaces and their location
	KeyspacesInfo []MigrationBackupKeyspaceInfo `json:"keyspaces_info"`

	// The UUID of the cluster
	ClusterId string `json:"cluster_id"`

	KmsSpec KmsSpec `json:"kms_spec"`
}
