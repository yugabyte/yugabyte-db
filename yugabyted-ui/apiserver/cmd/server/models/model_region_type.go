package models

// RegionType - Region type
type RegionType struct {

	SameRegion ZoneType `json:"same_region"`

	CrossRegion float64 `json:"cross_region"`

	Internet float64 `json:"internet"`
}
