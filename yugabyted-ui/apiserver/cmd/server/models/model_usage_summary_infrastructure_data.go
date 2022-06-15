package models

// UsageSummaryInfrastructureData - Usage summary data by infrastructure
type UsageSummaryInfrastructureData struct {

	InfrastructureList []UsageSummaryStatisticsData `json:"infrastructure_list"`
}
