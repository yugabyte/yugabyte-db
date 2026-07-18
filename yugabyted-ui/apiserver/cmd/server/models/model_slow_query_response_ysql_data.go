package models

// SlowQueryResponseYsqlData - Schema for Slow Query Response YSQL Data
type SlowQueryResponseYsqlData struct {

    // Count of Errors
    ErrorCount int32 `json:"error_count"`

    Queries []SlowQueryResponseYsqlQueryItem `json:"queries"`
}
