package models

// XClusterConfigSpec - XCluster Config Specification
type XClusterConfigSpec struct {

	// The name of the config to be created
	Name string `json:"name"`

	// The UUID of the source cluster
	SourceClusterId string `json:"source_cluster_id"`

	// UUID of the target cluster
	TargetClusterId string `json:"target_cluster_id"`

	// Names of Tables to be replicated
	TableIds []string `json:"table_ids"`
}
