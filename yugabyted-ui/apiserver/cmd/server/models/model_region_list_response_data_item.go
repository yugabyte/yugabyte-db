package models

// RegionListResponseDataItem - An object containing code and name of a region
type RegionListResponseDataItem struct {

	// Region code
	Code string `json:"code"`

	// Region name
	Name string `json:"name"`

	// Country code
	CountryCode string `json:"country_code"`
}
