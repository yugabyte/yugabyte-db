package models

// UpgradeRequestInfo - Upgrade Request Info
type UpgradeRequestInfo struct {

	// Task instance ID
	Id string `json:"id"`

	// Task type
	TaskType string `json:"task_type"`

	// Creation time
	ScheduledAt string `json:"scheduled_at"`

	// Upgrade version
	Version string `json:"version"`
}
