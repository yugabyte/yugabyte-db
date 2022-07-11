package models
// ClusterType : The kind of cluster deployment
type ClusterType string

// List of ClusterType
const (
	CLUSTERTYPE_SYNCHRONOUS ClusterType = "SYNCHRONOUS"
	CLUSTERTYPE_GEO_PARTITIONED ClusterType = "GEO_PARTITIONED"
)
