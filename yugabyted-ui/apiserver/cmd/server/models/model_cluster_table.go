package models

// ClusterTable - Model representing a DB table
type ClusterTable struct {

    Name string `json:"name"`

    Keyspace string `json:"keyspace"`

    Uuid string `json:"uuid"`

    Type YbApiEnum `json:"type"`

    SizeBytes int64 `json:"size_bytes"`
}
