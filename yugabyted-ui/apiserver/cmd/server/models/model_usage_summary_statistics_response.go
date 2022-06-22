package models

type UsageSummaryStatisticsResponse struct {

	ClusterData UsageSummaryClusterData `json:"cluster_data"`

	InfrastructureData UsageSummaryInfrastructureData `json:"infrastructure_data"`
}
