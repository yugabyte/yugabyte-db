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

func TestFindClusterByType(t *testing.T) {
	// Read replica first: the primary cluster is not always at index 0.
	clusters := []ybaclient.Cluster{
		newCluster(ReadReplicaClusterType),
		newCluster(PrimaryClusterType),
	}
	tests := []struct {
		name        string
		clusters    []ybaclient.Cluster
		clusterType string
		wantIdx     int
		wantType    string
	}{
		{"primary not at index 0", clusters, PrimaryClusterType, 1, PrimaryClusterType},
		{"case insensitive", clusters, "primary", 1, PrimaryClusterType},
		{"read replica", clusters, ReadReplicaClusterType, 0, ReadReplicaClusterType},
		{"missing primary", clusters[:1], PrimaryClusterType, -1, ""},
		{"nil clusters", nil, PrimaryClusterType, -1, ""},
	}
	for _, tt := range tests {
		got, idx := FindClusterByType(tt.clusters, tt.clusterType)
		if idx != tt.wantIdx || got.GetClusterType() != tt.wantType {
			t.Errorf("%s: got (%q, %d), want (%q, %d)",
				tt.name, got.GetClusterType(), idx, tt.wantType, tt.wantIdx)
		}
	}
}
