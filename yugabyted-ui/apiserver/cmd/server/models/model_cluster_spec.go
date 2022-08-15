package models

// ClusterSpec - Cluster spec
type ClusterSpec struct {

    // The name of the cluster
    Name string `json:"name"`

    CloudInfo CloudInfo `json:"cloud_info"`

    ClusterInfo ClusterInfo `json:"cluster_info"`

    NetworkInfo Networking `json:"network_info"`

    SoftwareInfo SoftwareInfo `json:"software_info"`

    ClusterRegionInfo *[]ClusterRegionInfo `json:"cluster_region_info"`

    EncryptionInfo EncryptionInfo `json:"encryption_info"`
}
