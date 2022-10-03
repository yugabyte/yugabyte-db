package models

type AccountQuotaPaidTier struct {

	// Maximum number of nodes allowed for a cluster
	MaxNumNodesPerCluster int32 `json:"max_num_nodes_per_cluster"`

	// Maximum number of vcpus allowed for a cluster
	MaxNumVcpusPerCluster int32 `json:"max_num_vcpus_per_cluster"`
}
