package models

// RateCardSpec - Rate card spec
type RateCardSpec struct {

	ProdVcpuDiscountPercentage float32 `json:"prod_vcpu_discount_percentage"`

	NonProdVcpuDiscountPercentage float32 `json:"non_prod_vcpu_discount_percentage"`

	ValidFrom string `json:"valid_from"`

	ValidUntil string `json:"valid_until"`
}
