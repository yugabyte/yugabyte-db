package models

// XClusterPlacementLocation - Get universe's placement location in xCluster replication.
type XClusterPlacementLocation struct {

    // Cloud name
    Cloud string `json:"cloud"`

    // Zone name
    Zone string `json:"zone"`

    // Region name
    Region string `json:"region"`
}
