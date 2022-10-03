package models

// EstimateClusterCostInfo - Estimate cluster cost data
type EstimateClusterCostInfo struct {

	EstimatedCostUsd float64 `json:"estimated_cost_usd"`

	AdditionalCostUsd float64 `json:"additional_cost_usd"`
}
