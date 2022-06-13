package models

// UpgradeRequestSpec - Additional spec for scheduled upgrades
type UpgradeRequestSpec struct {

	// Release ID to upgrade to (will default to latest if omitted)
	ReleaseId string `json:"release_id"`

	Filters UpgradeRequestFilters `json:"filters"`
}
