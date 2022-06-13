package models

// UsageSummaryDetail - usage summary
type UsageSummaryDetail struct {

	Name string `json:"name"`

	Description string `json:"description"`

	Unit string `json:"unit"`

	Quantity float64 `json:"quantity"`

	UnitPrice float64 `json:"unit_price"`

	Amount float64 `json:"amount"`
}
