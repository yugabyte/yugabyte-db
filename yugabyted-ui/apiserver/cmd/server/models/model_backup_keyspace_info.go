package models

// BackupKeyspaceInfo - Backup keyspace info
type BackupKeyspaceInfo struct {

	TableType YbApiEnum `json:"table_type"`

	// List of keyspaces
	Keyspaces []string `json:"keyspaces"`
}
