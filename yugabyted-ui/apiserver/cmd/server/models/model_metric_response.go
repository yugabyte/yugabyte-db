package models

type MetricResponse struct {

    Data []MetricData `json:"data"`

    // Start of range of results
    StartTimestamp int64 `json:"start_timestamp"`

    // End of range of results
    EndTimestamp int64 `json:"end_timestamp"`
}
