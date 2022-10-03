package models

// XClusterConfigData - XCluster Config Data
type XClusterConfigData struct {

	Spec XClusterConfigSpec `json:"spec"`

	Info XClusterConfigInfo `json:"info"`
}
