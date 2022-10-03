package models

type BillingTypeSpec struct {

	Type int32 `json:"type"`

	StartDate string `json:"start_date"`

	EndDate string `json:"end_date"`

	SubscribedCapacitySummary SubscriptionCapacity `json:"subscribed_capacity_summary"`

	Threshold int32 `json:"threshold"`
}
