package models

// MigrationRestoreData - Restore Backup data
type MigrationRestoreData struct {

	Spec MigrationRestoreSpec `json:"spec"`

	Info MigrationRestoreInfo `json:"info"`
}
