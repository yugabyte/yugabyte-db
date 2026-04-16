package models

// SqlObjectMetadata - size, count, iops details of tables and indexes
type SqlObjectMetadata struct {

    ObjectName string `json:"object_name"`

    SqlType string `json:"sql_type"`

    RowCount int64 `json:"row_count"`

    Size int64 `json:"size"`

    Iops int64 `json:"iops"`
}
