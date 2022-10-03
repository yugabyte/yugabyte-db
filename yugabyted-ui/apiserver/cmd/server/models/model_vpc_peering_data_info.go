package models

type VpcPeeringDataInfo struct {

	Id string `json:"id"`

	State PeeringStateEnum `json:"state"`

	YugabyteVpcName string `json:"yugabyte_vpc_name"`
}
