/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"strings"
	"testing"
)

func TestBuildOCIRegionsKeepsMatchingZone(t *testing.T) {
	regions := buildOCIRegions(
		[]string{"region-name=us-sanjose-1::vnet=ocid1.vcn.oc1..example"},
		[]string{
			"zone-name=us-sanjose-1-AD-1::region-name=us-sanjose-1::subnet=ocid1.subnet.oc1..example",
			"zone-name=us-sanjose-1-AD-2::region-name=us-sanjose-1::subnet=ocid1.subnet.oc1..example2",
		},
	)
	if len(regions) != 1 {
		t.Fatalf("regions = %d, want 1", len(regions))
	}
	zones := regions[0].GetZones()
	if len(zones) != 2 {
		t.Fatalf("zones = %d, want 2", len(zones))
	}
	got := []string{zones[0].GetCode(), zones[1].GetCode()}
	want := "us-sanjose-1-AD-1,us-sanjose-1-AD-2"
	if strings.Join(got, ",") != want {
		t.Fatalf("zone codes = %v, want %s", got, want)
	}
}
