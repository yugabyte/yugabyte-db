package models

// LiveQueryResponseYcqlData - Schema for Live Query Response YCQL Data
type LiveQueryResponseYcqlData struct {

    // Count of Errors
    ErrorCount int32 `json:"error_count"`

    Queries []LiveQueryResponseYcqlQueryItem `json:"queries"`
}
