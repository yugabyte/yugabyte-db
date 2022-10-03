package models

type SingleTenantVpcDataResponseInfo struct {

	Id string `json:"id"`

	CloudProviderProject string `json:"cloud_provider_project"`

	ClusterIds []string `json:"cluster_ids"`

	PeeringIds []string `json:"peering_ids"`

	State VpcStateEnum `json:"state"`

	ExternalVpcId *string `json:"external_vpc_id"`
}
