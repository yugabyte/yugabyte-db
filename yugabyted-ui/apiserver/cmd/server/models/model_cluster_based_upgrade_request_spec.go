package models

// ClusterBasedUpgradeRequestSpec - Upgrade Spec for Cluster Based Upgrade
type ClusterBasedUpgradeRequestSpec struct {

	// Release ID to upgrade to (will default to latest if omitted)
	ReleaseId string `json:"release_id"`
}
