package models

type AccountQuotaFreeTier struct {

	// Maximum number of free tier clusters allowed to exist simultaneously
	MaxNumClusters int32 `json:"max_num_clusters"`

	// Number of additional free tier clusters allowed to be created
	NumClustersRemaining int32 `json:"num_clusters_remaining"`
}
