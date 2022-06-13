package models

// UsageSummaryData - Usage summary data
type UsageSummaryData struct {

	ClusterList []ClusterUsageSummaryData `json:"cluster_list"`
}
