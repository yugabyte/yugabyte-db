package models

type BackupResponse struct {
    Backup []BackupDetails `json:"backup"`
}
