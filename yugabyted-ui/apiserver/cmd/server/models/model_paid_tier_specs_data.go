package models

// PaidTierSpecsData - Paid tier specifications
type PaidTierSpecsData struct {

	PerCoreCostCentsPerHr int32 `json:"per_core_cost_cents_per_hr"`

	Apis []YbApiEnum `json:"apis"`
}
