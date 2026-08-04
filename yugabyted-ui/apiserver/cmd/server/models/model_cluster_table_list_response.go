package models

type ClusterTableListResponse struct {

    // List of cluster tables
    Tables []ClusterTable `json:"tables"`

    // List of cluster tables
    Indexes []ClusterTable `json:"indexes"`
}
