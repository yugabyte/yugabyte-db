package models
// ClusterFaultTolerance : The level of fault tolerance for the cluster
type ClusterFaultTolerance string

// List of ClusterFaultTolerance
const (
    CLUSTERFAULTTOLERANCE_NONE ClusterFaultTolerance = "NONE"
    CLUSTERFAULTTOLERANCE_NODE ClusterFaultTolerance = "NODE"
    CLUSTERFAULTTOLERANCE_ZONE ClusterFaultTolerance = "ZONE"
    CLUSTERFAULTTOLERANCE_REGION ClusterFaultTolerance = "REGION"
)
