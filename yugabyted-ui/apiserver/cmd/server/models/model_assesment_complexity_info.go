package models

// AssesmentComplexityInfo - Assesment complexity details
type AssesmentComplexityInfo struct {

    Schema string `json:"schema"`

    SqlObjectsCount int32 `json:"sql_objects_count"`

    TableCount int32 `json:"table_count"`

    Complexity string `json:"complexity"`
}
