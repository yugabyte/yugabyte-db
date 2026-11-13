/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cluster

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
)

func TestCheckNumNodesMatchZones(t *testing.T) {
	az := func(n int32) ybaclient.PlacementAZ {
		a := ybaclient.PlacementAZ{}
		a.SetNumNodesInAZ(n)
		return a
	}
	region := ybaclient.PlacementRegion{}
	region.SetAzList([]ybaclient.PlacementAZ{az(2), az(1)})
	cloud := ybaclient.PlacementCloud{}
	cloud.SetRegionList([]ybaclient.PlacementRegion{region})
	cloudList := []ybaclient.PlacementCloud{cloud}

	if err := checkNumNodesMatchZones(3, cloudList); err != nil {
		t.Errorf("3 nodes in zones 2+1: got error %v, want nil", err)
	}
	if err := checkNumNodesMatchZones(6, cloudList); err == nil {
		t.Errorf("6 nodes in zones 2+1: got nil, want error")
	}
}
