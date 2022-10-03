package models

// PrivateClusterSpec - Private Cluster spec
type PrivateClusterSpec struct {

	// The name of the cluster
	Name string `json:"name"`

	ClusterInfo PrivateClusterInfo `json:"cluster_info"`

	ClusterRegionInfo []ClusterRegionInfo `json:"cluster_region_info"`

	SoftwareInfo SoftwareInfo `json:"software_info"`
}
