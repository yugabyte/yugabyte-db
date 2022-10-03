package models

// ScheduledClusterUpgradeTaskData - Scheduled Cluster upgrade task data
type ScheduledClusterUpgradeTaskData struct {

	Spec ClusterBasedUpgradeRequestSpec `json:"spec"`

	Info UpgradeRequestInfo `json:"info"`
}
