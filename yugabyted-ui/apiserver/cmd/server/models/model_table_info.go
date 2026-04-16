package models

// TableInfo - Table Info
type TableInfo struct {

    TableName string `json:"table_name"`

    TableId string `json:"table_id"`

    TableVersion int32 `json:"table_version"`

    TableType string `json:"table_type"`

    TableState string `json:"table_state"`

    TableStateMessage string `json:"table_state_message"`

    TableTablespaceOid string `json:"table_tablespace_oid"`

    TableReplicationInfo TableInfoTableReplicationInfo `json:"table_replication_info"`

    Columns []ColumnInfo `json:"columns"`

    Tablets []TabletInfo `json:"tablets"`
}
