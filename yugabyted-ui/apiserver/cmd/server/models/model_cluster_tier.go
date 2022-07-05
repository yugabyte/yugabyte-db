package models
// ClusterTier : Which tier the cluster is in
type ClusterTier string

// List of ClusterTier
const (
	CLUSTERTIER_FREE ClusterTier = "FREE"
	CLUSTERTIER_PAID ClusterTier = "PAID"
)
