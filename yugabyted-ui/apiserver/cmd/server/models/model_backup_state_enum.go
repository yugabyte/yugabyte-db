package models
// BackupStateEnum : State of the backup
type BackupStateEnum string

// List of BackupStateEnum
const (
	BACKUPSTATEENUM_SUCCEEDED BackupStateEnum = "SUCCEEDED"
	BACKUPSTATEENUM_FAILED BackupStateEnum = "FAILED"
	BACKUPSTATEENUM_IN_PROGRESS BackupStateEnum = "IN_PROGRESS"
)
