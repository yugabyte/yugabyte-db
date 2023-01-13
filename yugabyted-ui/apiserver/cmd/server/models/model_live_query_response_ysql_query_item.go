package models

// LiveQueryResponseYsqlQueryItem - Schema for Live Query Response YSQL Query Item
type LiveQueryResponseYsqlQueryItem struct {

    Id string `json:"id"`

    NodeName string `json:"node_name"`

    DbName string `json:"db_name"`

    SessionStatus string `json:"session_status"`

    Query string `json:"query"`

    ElapsedMillis int64 `json:"elapsed_millis"`

    QueryStartTime string `json:"query_start_time"`

    AppName string `json:"app_name"`

    ClientHost string `json:"client_host"`

    ClientPort string `json:"client_port"`
}
