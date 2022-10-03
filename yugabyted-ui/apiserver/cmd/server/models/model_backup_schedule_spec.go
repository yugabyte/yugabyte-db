package models

// BackupScheduleSpec - Backup schedule spec
type BackupScheduleSpec struct {

	BackupSpec BackupSpec `json:"backup_spec"`

	ScheduleSpec ScheduleSpec `json:"schedule_spec"`
}
