package models

type ClusterDataInfo struct {

    // The UUID of the universe
    UniverseUuid string `json:"universe_uuid"`

    // The UUID of the cluster
    ClusterUuid string `json:"cluster_uuid"`

    // The current state of the cluster
    State string `json:"state"`

    // The current version of YugabyteDB installed on the cluster
    SoftwareVersion string `json:"software_version"`

    Metadata EntityMetadata `json:"metadata"`
}
