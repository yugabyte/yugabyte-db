package models

// NetworkData - Network data
type NetworkData struct {

	Ingress RegionType `json:"ingress"`

	Egress RegionType `json:"egress"`
}
