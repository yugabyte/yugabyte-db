package models

// GlobalRateCardMap - Global rate card map for type PROD or NON_PROD
type GlobalRateCardMap struct {

	// Base product rate
	BaseProductRate float32 `json:"base_product_rate"`

	OverageRate GlobalRateCardMapOverageRate `json:"overage_rate"`

	// Burstable vCPU rate
	BurstableVcpuRate float32 `json:"burstable_vcpu_rate"`

	// Paused cluster disk storage rate
	PausedClusterDiskStorageRate float32 `json:"paused_cluster_disk_storage_rate"`

	// Describes whether the rate is for prod or non-prod
	RateFor string `json:"rate_for"`
}
