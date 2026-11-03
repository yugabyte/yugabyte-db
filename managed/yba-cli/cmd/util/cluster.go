/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
)

// FindClusterByType returns the cluster of the given type and its index in clusters.
// If no cluster matches, it returns an empty cluster and -1.
// Do not assume the primary cluster is at index 0.
func FindClusterByType(
	clusters []ybaclient.Cluster,
	clusterType string,
) (ybaclient.Cluster, int) {
	for idx, cluster := range clusters {
		if strings.EqualFold(cluster.GetClusterType(), clusterType) {
			return cluster, idx
		}
	}
	logrus.Debug("No cluster found with type: ", clusterType)
	return ybaclient.Cluster{}, -1
}
