package models

type SubscriptionCapacity struct {

	ProdVcpus int32 `json:"prod_vcpus"`

	NonProdVcpus int32 `json:"non_prod_vcpus"`
}
