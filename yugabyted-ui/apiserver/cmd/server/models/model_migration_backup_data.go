package models

// MigrationBackupData - Migration backup data
type MigrationBackupData struct {

	Spec MigrationBackupSpec `json:"spec"`

	Info MigrationBackupInfo `json:"info"`
}
