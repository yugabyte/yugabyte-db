package models

type VpcPeeringData struct {

	Spec VpcPeeringSpec `json:"spec"`

	Info VpcPeeringDataInfo `json:"info"`
}
