/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
)

// FindClusterIndexByType returns the index of the cluster by type, or -1 if not found.
// Do not assume the primary cluster is at index 0.
func FindClusterIndexByType(clusters []ybaclient.Cluster, clusterType string) int {
	for idx, cluster := range clusters {
		if strings.EqualFold(cluster.GetClusterType(), clusterType) {
			return idx
		}
	}
	logrus.Debug("No cluster found with type: ", clusterType)
	return -1
}

// FindClusterByType returns the cluster by type, or an empty cluster if not found
func FindClusterByType(clusters []ybaclient.Cluster, clusterType string) ybaclient.Cluster {
	idx := FindClusterIndexByType(clusters, clusterType)
	if idx < 0 {
		return ybaclient.Cluster{}
	}
	return clusters[idx]
}
