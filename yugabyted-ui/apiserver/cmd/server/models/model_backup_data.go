package models

// BackupData - Backup data
type BackupData struct {

	Spec BackupSpec `json:"spec"`

	Info BackupInfo `json:"info"`
}
