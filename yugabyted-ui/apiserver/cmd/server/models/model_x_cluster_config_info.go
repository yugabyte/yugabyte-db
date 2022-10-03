package models

// XClusterConfigInfo - XCluster Config Information
type XClusterConfigInfo struct {

	// The UUID of the XCluster Config
	Id string `json:"id"`

	State XClusterConfigStateEnum `json:"state"`

	Metadata EntityMetadata `json:"metadata"`
}
