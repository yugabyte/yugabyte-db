package models

// AssessmentSourceDbObject - Details about Source DB's SQL Objects
type AssessmentSourceDbObject struct {

    SqlObjectsCount []SqlObjectCount `json:"sql_objects_count"`

    SqlObjectsMetadata []SqlObjectMetadata `json:"sql_objects_metadata"`
}
