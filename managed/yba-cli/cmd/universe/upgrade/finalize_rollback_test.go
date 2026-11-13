/*
 * Copyright (c) YugabyteDB, Inc.
 */

package upgrade

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestImpactedUniverses(t *testing.T) {
	if got := impactedUniverses(nil); got != "" {
		t.Errorf("nil info: got %q, want empty", got)
	}
	empty := &ybaclient.FinalizeUpgradeInfoResponse{}
	if got := impactedUniverses(empty); got != "" {
		t.Errorf("no impacted universes: got %q, want empty", got)
	}
	info := &ybaclient.FinalizeUpgradeInfoResponse{
		ImpactedXClusterConnectedUniverse: []ybaclient.ImpactedXClusterConnectedUniverse{
			{
				UniverseName:      util.GetStringPointer("target-1"),
				YbSoftwareVersion: util.GetStringPointer("2025.2.1.0-b1"),
			},
			{
				UniverseName:      util.GetStringPointer("target-2"),
				YbSoftwareVersion: util.GetStringPointer("2025.2.2.0-b5"),
			},
		},
	}
	want := "target-1 (2025.2.1.0-b1), target-2 (2025.2.2.0-b5)"
	if got := impactedUniverses(info); got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestRollbackVersions(t *testing.T) {
	userIntent := ybaclient.UserIntent{}
	userIntent.SetYbSoftwareVersion("2026.1.0.0-b10")
	readReplicaIntent := ybaclient.UserIntent{}
	readReplicaIntent.SetYbSoftwareVersion("wrong-cluster")
	clusters := []ybaclient.Cluster{
		{ClusterType: util.ReadReplicaClusterType, UserIntent: readReplicaIntent},
		{ClusterType: util.PrimaryClusterType, UserIntent: userIntent},
	}

	details := ybaclient.UniverseDefinitionTaskParamsResp{Clusters: clusters}
	universe := ybaclient.UniverseResp{UniverseDetails: &details}
	current, previous := rollbackVersions(universe)
	if current != "2026.1.0.0-b10" || previous != "" {
		t.Errorf("no prev config: got (%q, %q), want (%q, %q)",
			current, previous, "2026.1.0.0-b10", "")
	}

	prev := ybaclient.PrevYBSoftwareConfig{}
	prev.SetSoftwareVersion("2025.2.3.0-b4")
	details.SetPrevYBSoftwareConfig(prev)
	universe.SetUniverseDetails(details)
	current, previous = rollbackVersions(universe)
	if current != "2026.1.0.0-b10" || previous != "2025.2.3.0-b4" {
		t.Errorf("with prev config: got (%q, %q), want (%q, %q)",
			current, previous, "2026.1.0.0-b10", "2025.2.3.0-b4")
	}
}
