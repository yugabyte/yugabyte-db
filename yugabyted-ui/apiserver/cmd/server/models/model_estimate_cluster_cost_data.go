package models

// EstimateClusterCostData - Estimate cluster cost data
type EstimateClusterCostData struct {

	Spec EstimateClusterCostSpec `json:"spec"`

	Info EstimateClusterCostInfo `json:"info"`
}
