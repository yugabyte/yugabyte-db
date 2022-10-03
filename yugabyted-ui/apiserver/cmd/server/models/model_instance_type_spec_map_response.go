package models

type InstanceTypeSpecMapResponse struct {

	// A map of region codes to available HW specs
	Data map[string][]InstanceTypeSpecResponseItem `json:"data"`
}
