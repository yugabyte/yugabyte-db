package models

type ClusterDataInfo struct {

	// The UUID of the cluster
	Id string `json:"id"`

	// The current state of the cluster
	State string `json:"state"`

	// Endpoint to connect to the cluster
	Endpoint *string `json:"endpoint"`

	// Endpoints to connect to the cluster by region
	Endpoints *map[string]string `json:"endpoints"`

	// ID of the associated project
	ProjectId string `json:"project_id"`

	// The current version of YugabyteDB installed on the cluster
	SoftwareVersion string `json:"software_version"`

	Metadata EntityMetadata `json:"metadata"`
}
