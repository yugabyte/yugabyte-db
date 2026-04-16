package helpers

import (
    "encoding/json"
    "net/url"
    "strconv"
)

type OnDiskSizeStruct struct {
    WalFilesSize                 string `json:"wal_files_size"`
    WalFilesSizeBytes            int64  `json:"wal_files_size_bytes"`
    SstFilesSize                 string `json:"sst_files_size"`
    SstFilesSizeBytes            int64  `json:"sst_files_size_bytes"`
    UncompressedSstFileSize      string `json:"uncompressed_sst_file_size"`
    UncompressedSstFileSizeBytes int64  `json:"uncompressed_sst_file_size_bytes"`
    HasMissingSize               bool   `json:"has_missing_size"`
}

type TableStruct struct {
    Keyspace       string           `json:"keyspace"`
    TableName      string           `json:"table_name"`
    State          string           `json:"state"`
    Message        string           `json:"message"`
    Uuid           string           `json:"uuid"`
    YsqlOid        string           `json:"ysql_oid"`
    ParentOid      string           `json:"parent_oid"`
    ColocationId   string           `json:"colocation_id"`
    OnDiskSize     OnDiskSizeStruct `json:"on_disk_size"`
    HasMissingSize bool             `json:"has_missing_size"`
    Hidden         bool             `json:"hidden"`
}

type TablesResponseStruct struct {
    User   []TableStruct `json:"user"`
    Index  []TableStruct `json:"index"`
    Parent []TableStruct `json:"parent"`
    System []TableStruct `json:"system"`
}

type Table struct {
    Keyspace  string
    Name      string
    SizeBytes int64
    IsYsql    bool
}

type TablesFuture struct {
    Tables TablesResponseStruct
    Error  error
}

func (h *HelperContainer) GetTablesFuture(
    nodeHost string,
    onlyUserTables bool,
    future chan TablesFuture,
) {
    tables := TablesFuture{
        Tables: TablesResponseStruct{},
        Error: nil,
    }
    params := url.Values{}
    params.Add("only_user_tables", strconv.FormatBool(onlyUserTables))
    body, err := h.BuildMasterURLsAndAttemptGetRequests(
        "api/v1/tables", // path
        params, // params
        true, // expectJson
    )
    if err != nil {
        tables.Error = err
        future <- tables
        return
    }
    err = json.Unmarshal([]byte(body), &tables.Tables)
    tables.Error = err
    future <- tables
}

// Type alias from cluster_config_http_request.go
type TableReplicationInfo = ReplicationInfoStruct

type ColumnInfo struct {
    Column string `json:"column"`
    Id     string `json:"id"`
    Type   string `json:"type"`
}

type RaftConfig struct {
    Uuid     string `json:"uuid"`
    Role     string `json:"role"`
    Location string `json:"location"`
}

type TableTabletInfo struct {
    TabletId   string       `json:"tablet_id"`
    Partition  string       `json:"partition"`
    SplitDepth int32        `json:"split_depth"`
    State      string       `json:"state"`
    Hidden     string       `json:"hidden"`
    Message    string       `json:"message"`
    Locations  []RaftConfig `json:"locations"`
}

type TableInfoStruct struct {
    TableName            string               `json:"table_name"`
    TableId              string               `json:"table_id"`
    TableVersion         int32                `json:"table_version"`
    TableType            string               `json:"table_type"`
    TableState           string               `json:"table_state"`
    TableStateMessage    string               `json:"table_state_message"`
    TableTablespaceOid   string               `json:"table_tablespace_oid"`
    TableReplicationInfo TableReplicationInfo `json:"table_replication_info"`
    Columns              []ColumnInfo         `json:"columns"`
    Tablets              []TableTabletInfo    `json:"tablets"`
}

type TableInfoFuture struct {
    TableInfo TableInfoStruct
    Error     error
}

// Get info for a table given the table id
func (h *HelperContainer) GetTableInfoFuture(
    nodeHost string,
    id string,
    future chan TableInfoFuture,
) {
    tableInfo := TableInfoFuture{
        TableInfo: TableInfoStruct{},
        Error: nil,
    }
    params := url.Values{}
    params.Add("id", id)
    body, err := h.BuildMasterURLsAndAttemptGetRequests(
        "api/v1/table", // path
        params, // params
        true, // expectJson
    )
    if err != nil {
        tableInfo.Error = err
        future <- tableInfo
        return
    }
    err = json.Unmarshal([]byte(body), &tableInfo.TableInfo)
    tableInfo.Error = err
    future <- tableInfo
}
