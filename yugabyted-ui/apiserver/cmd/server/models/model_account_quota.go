package models

type AccountQuota struct {

	FreeTier AccountQuotaFreeTier `json:"free_tier"`

	PaidTier AccountQuotaPaidTier `json:"paid_tier"`
}
