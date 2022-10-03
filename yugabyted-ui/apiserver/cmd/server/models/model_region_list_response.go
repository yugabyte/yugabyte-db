package models

type RegionListResponse struct {

	// A list of regions that a cluster can be deployed in
	Data []RegionListResponseDataItem `json:"data"`
}
