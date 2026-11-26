/*
 * Copyright (c) YugabyteDB, Inc.
 */

package azu

import (
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestAzureRemoveZoneLeavesActiveZone(t *testing.T) {
	const removeZone = "zone-name=z1::region-name=r1"
	rejecting := editAzureRegions(nil, nil, nil, []string{removeZone}, []ybaclient.Region{azureRegion()})
	err := providerutil.ValidateZoneRemovalLeavesActiveZone([]string{removeZone}, rejecting)
	if err == nil || !strings.Contains(err.Error(), "region r1 would have no active zone") {
		t.Fatalf("got error %v, want the region rejected", err)
	}

	removed := removeAzureRegions([]string{"r1"}, []ybaclient.Region{azureRegion()})
	removed = editAzureRegions(nil, nil, nil, []string{removeZone}, removed)
	if err := providerutil.ValidateZoneRemovalLeavesActiveZone([]string{removeZone}, removed); err != nil {
		t.Fatalf("removing the region too should be allowed: %v", err)
	}
}

func azureRegion() ybaclient.Region {
	zone := ybaclient.AvailabilityZone{Code: util.GetStringPointer("z1"), Name: "z1"}
	zone.SetActive(true)
	region := ybaclient.Region{
		Code:  util.GetStringPointer("r1"),
		Zones: []ybaclient.AvailabilityZone{zone},
	}
	region.SetActive(true)
	return region
}
