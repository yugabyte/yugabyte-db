package models

// VmUpgradeRequest - Request to upgrade VM image of a cluster
type VmUpgradeRequest struct {

	// VM Image region map data
	VmImage map[string]string `json:"vm_image"`

	ForceUpgrade bool `json:"force_upgrade"`
}
