package models
// XClusterConfigStateEnum : XCluster Config State Enum
type XClusterConfigStateEnum string

// List of XClusterConfigStateEnum
const (
	XCLUSTERCONFIGSTATEENUM_CREATING XClusterConfigStateEnum = "CREATING"
	XCLUSTERCONFIGSTATEENUM_UPDATING XClusterConfigStateEnum = "UPDATING"
	XCLUSTERCONFIGSTATEENUM_ACTIVE XClusterConfigStateEnum = "ACTIVE"
	XCLUSTERCONFIGSTATEENUM_FAILED XClusterConfigStateEnum = "FAILED"
	XCLUSTERCONFIGSTATEENUM_DELETING XClusterConfigStateEnum = "DELETING"
	XCLUSTERCONFIGSTATEENUM_PAUSED XClusterConfigStateEnum = "PAUSED"
)
