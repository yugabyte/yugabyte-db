package models

// LiveQueryResponseYsqlData - Schema for Live Query Response YSQL Data
type LiveQueryResponseYsqlData struct {

    // Count of Errors
    ErrorCount int32 `json:"error_count"`

    Queries []LiveQueryResponseYsqlQueryItem `json:"queries"`
}
