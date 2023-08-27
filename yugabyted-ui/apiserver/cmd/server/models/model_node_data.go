package models

// NodeData - Node data
type NodeData struct {

    Name string `json:"name"`

    Host string `json:"host"`

    IsNodeUp bool `json:"is_node_up"`

    IsMaster bool `json:"is_master"`

    IsTserver bool `json:"is_tserver"`

    IsReadReplica bool `json:"is_read_replica"`

    IsMasterUp bool `json:"is_master_up"`

    IsBootstrapping bool `json:"is_bootstrapping"`

    Metrics NodeDataMetrics `json:"metrics"`

    CloudInfo NodeDataCloudInfo `json:"cloud_info"`

    SoftwareVersion string `json:"software_version"`
}
