package models

// UnsupportedSqlObjectData - Unsupported SQL Object Data
type UnsupportedSqlObjectData struct {

    ObjectType string `json:"object_type"`

    ObjectName string `json:"object_name"`

    SqlStatement string `json:"sql_statement"`
}
