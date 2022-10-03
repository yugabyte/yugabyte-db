package models

// GlobalRateCardSpec - Global rate card spec
type GlobalRateCardSpec struct {

	Type int32 `json:"type"`

	Rate []GlobalRateCardMap `json:"rate"`
}
