package models

// BackupMeteringEvent - Backup metering event
type BackupMeteringEvent struct {

	StartTimestamp string `json:"start_timestamp"`

	PeriodInMins float64 `json:"period_in_mins"`

	TotalBackupSizeInBytes int64 `json:"total_backup_size_in_bytes"`
}
