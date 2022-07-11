package models

// UsageSummaryClusterData - Usage summary data by cluster
type UsageSummaryClusterData struct {

	ClusterList []UsageSummaryStatisticsData `json:"cluster_list"`
}
