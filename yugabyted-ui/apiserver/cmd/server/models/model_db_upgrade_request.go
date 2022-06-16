package models

// DbUpgradeRequest - Request to upgrade DB version of a cluster
type DbUpgradeRequest struct {

	ReleaseId string `json:"release_id"`

	DowngradeOverride bool `json:"downgrade_override"`
}
