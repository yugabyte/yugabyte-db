package models

// CreateClusterRequest - Create cluster request
type CreateClusterRequest struct {

	ClusterSpec ClusterSpec `json:"cluster_spec"`

	DbCredentials CreateClusterRequestDbCredentials `json:"db_credentials"`
}
