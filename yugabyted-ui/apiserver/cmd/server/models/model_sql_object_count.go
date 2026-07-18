package models

// SqlObjectCount - Count of specific Sql Object type in the source DB
type SqlObjectCount struct {

    SqlType string `json:"sql_type"`

    Count int32 `json:"count"`
}
