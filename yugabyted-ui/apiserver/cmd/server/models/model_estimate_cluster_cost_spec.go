package models

// EstimateClusterCostSpec - Estimate Cluster Cost Spec
type EstimateClusterCostSpec struct {

	CloudProvider CloudEnum `json:"cloud_provider"`

	Region string `json:"region"`

	NumNodes int32 `json:"num_nodes"`

	IsProductionCluster bool `json:"is_production_cluster"`

	NodeConfig NodeConfigSpec `json:"node_config"`
}
