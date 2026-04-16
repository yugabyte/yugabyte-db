package models

// SourceDatabaseInfo - Source Database details
type SourceDatabaseInfo struct {

    TableSize int64 `json:"table_size"`

    TableRowCount int64 `json:"table_row_count"`

    TotalTableSize int64 `json:"total_table_size"`

    TotalIndexSize int64 `json:"total_index_size"`
}
