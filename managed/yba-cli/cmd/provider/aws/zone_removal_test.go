/*
 * Copyright (c) YugabyteDB, Inc.
 */

package aws

import (
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestAWSRemoveZoneLeavesActiveZone(t *testing.T) {
	const removeZone = "zone-name=z1::region-name=r1"
	rejecting := editAWSRegions(nil, nil, nil, []string{removeZone}, []ybaclient.Region{awsRegion(true)})
	err := providerutil.ValidateZoneRemovalLeavesActiveZone([]string{removeZone}, rejecting)
	if err == nil || !strings.Contains(err.Error(), "region r1 would have no active zone") {
		t.Fatalf("got error %v, want the region rejected", err)
	}

	removed := removeAWSRegions([]string{"r1"}, []ybaclient.Region{awsRegion(true)})
	removed = editAWSRegions(nil, nil, nil, []string{removeZone}, removed)
	if err := providerutil.ValidateZoneRemovalLeavesActiveZone([]string{removeZone}, removed); err != nil {
		t.Fatalf("removing the region too should be allowed: %v", err)
	}
}

func awsRegion(active bool) ybaclient.Region {
	zone := ybaclient.AvailabilityZone{Code: util.GetStringPointer("z1"), Name: "z1"}
	zone.SetActive(true)
	region := ybaclient.Region{
		Code:  util.GetStringPointer("r1"),
		Zones: []ybaclient.AvailabilityZone{zone},
	}
	region.SetActive(active)
	return region
}
