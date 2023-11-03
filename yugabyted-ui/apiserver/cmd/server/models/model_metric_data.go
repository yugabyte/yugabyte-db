package models

// MetricData - Metric data
type MetricData struct {

    // The name of the metric
    Name string `json:"name"`

    // Array of (timestamp, value) tuples
    Values [][]float64 `json:"values"`
}
