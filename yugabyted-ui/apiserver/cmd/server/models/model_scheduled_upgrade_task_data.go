package models

// ScheduledUpgradeTaskData - Scheduled upgrade task data
type ScheduledUpgradeTaskData struct {

	Spec UpgradeRequestSpec `json:"spec"`

	Info UpgradeRequestInfo `json:"info"`
}
