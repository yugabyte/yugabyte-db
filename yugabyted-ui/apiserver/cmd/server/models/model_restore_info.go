package models

// RestoreInfo - Restore Info
type RestoreInfo struct {

	// The UUID of the restore
	Id string `json:"id"`

	// The UUID of the project
	ProjectId string `json:"project_id"`

	// The name of the cluster from which backup was taken
	SourceClusterName string `json:"source_cluster_name"`

	// The name of the cluster to which backup is being restored
	TargetClusterName string `json:"target_cluster_name"`

	// Description of the restore operation
	Description string `json:"description"`

	KeyspaceInfo []BackupKeyspaceInfo `json:"keyspace_info"`

	State RestoreStateEnum `json:"state"`

	Metadata EntityMetadata `json:"metadata"`

	SizeInBytes *int64 `json:"size_in_bytes"`
}
