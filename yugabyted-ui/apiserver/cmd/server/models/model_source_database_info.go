package models

// SourceDatabaseInfo - Source Database details
type SourceDatabaseInfo struct {

    TableSize int32 `json:"table_size"`

    TableRowCount int32 `json:"table_row_count"`

    TotalTableSize int32 `json:"total_table_size"`

    TotalIndexSize int32 `json:"total_index_size"`
}
