package models

// GflagsUpgradeRequest - Request to upgrade gflags of a cluster
type GflagsUpgradeRequest struct {

	// GFlags map data
	MasterGflags map[string]string `json:"master_gflags"`

	// GFlags map data
	TserverGflags map[string]string `json:"tserver_gflags"`
}
