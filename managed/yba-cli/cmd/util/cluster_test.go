/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
)

func newCluster(clusterType string) ybaclient.Cluster {
	c := ybaclient.Cluster{}
	c.SetClusterType(clusterType)
	return c
}

func TestFindClusterIndexByType(t *testing.T) {
	// Read replica first: the primary cluster is not always at index 0.
	clusters := []ybaclient.Cluster{
		newCluster(ReadReplicaClusterType),
		newCluster(PrimaryClusterType),
	}
	if got := FindClusterIndexByType(clusters, PrimaryClusterType); got != 1 {
		t.Errorf("primary index = %d, want 1", got)
	}
	if got := FindClusterIndexByType(clusters, "primary"); got != 1 {
		t.Errorf("case-insensitive primary index = %d, want 1", got)
	}
	if got := FindClusterIndexByType(clusters, ReadReplicaClusterType); got != 0 {
		t.Errorf("read replica index = %d, want 0", got)
	}
	if got := FindClusterIndexByType(clusters[:1], PrimaryClusterType); got != -1 {
		t.Errorf("missing primary index = %d, want -1", got)
	}
	if got := FindClusterByType(clusters, PrimaryClusterType); got.GetClusterType() !=
		PrimaryClusterType {
		t.Errorf("FindClusterByType returned type %q", got.GetClusterType())
	}
	if got := FindClusterByType(nil, PrimaryClusterType); got.GetClusterType() != "" {
		t.Errorf("FindClusterByType on nil returned type %q", got.GetClusterType())
	}
}
