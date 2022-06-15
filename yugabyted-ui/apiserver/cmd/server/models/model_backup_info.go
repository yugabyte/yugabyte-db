package models

// BackupInfo - Backup data
type BackupInfo struct {

	// The UUID of the backup
	Id string `json:"id"`

	// The name of the cluster
	ClusterName string `json:"cluster_name"`

	State BackupStateEnum `json:"state"`

	ActionType BackupActionTypeEnum `json:"action_type"`

	KeyspaceInfo []BackupKeyspaceInfo `json:"keyspace_info"`

	SizeInBytes *int64 `json:"size_in_bytes"`

	// Timestamp when the backup was deleted
	DeletedOn *string `json:"deleted_on"`

	// Timestamp when the backup creation completed
	CompletedOn *string `json:"completed_on"`

	Metadata EntityMetadata `json:"metadata"`
}
