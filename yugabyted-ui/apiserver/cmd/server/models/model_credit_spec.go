package models

// CreditSpec - Add credits spec
type CreditSpec struct {

	TotalCredits int32 `json:"total_credits"`

	ValidFrom string `json:"valid_from"`

	ValidUntil string `json:"valid_until"`

	CreditedBy string `json:"credited_by"`

	Reason string `json:"reason"`
}
