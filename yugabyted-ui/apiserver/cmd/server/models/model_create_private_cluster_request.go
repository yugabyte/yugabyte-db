package models

// CreatePrivateClusterRequest - Create private cluster request
type CreatePrivateClusterRequest struct {

	ClusterSpec PrivateClusterSpec `json:"cluster_spec"`

	DbCredentials CreateClusterRequestDbCredentials `json:"db_credentials"`
}
