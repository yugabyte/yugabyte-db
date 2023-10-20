package models

// SqlObjectsDetails - Sql obejcts type and count
type SqlObjectsDetails struct {

    ObjectType string `json:"objectType"`

    TotalCount int32 `json:"totalCount"`

    InvalidCount int32 `json:"invalidCount"`

    ObjectNames string `json:"objectNames"`

    ObjectDetails string `json:"objectDetails"`
}
