package models

// GFlagsData - Gflags Data
type GFlagsData struct {

	// GFlags map data
	MasterGflags map[string]string `json:"master_gflags"`

	// GFlags map data
	TserverGflags map[string]string `json:"tserver_gflags"`
}
