package models

// MigrationBackupKeyspaceInfo - Migration backup keyspace info
type MigrationBackupKeyspaceInfo struct {

	// Type of the table
	TableType string `json:"table_type"`

	// Name of the keyspace
	Keyspace string `json:"keyspace"`

	// Location of the keyspace
	Location string `json:"location"`
}
