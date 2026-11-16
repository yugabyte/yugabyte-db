/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"strings"
	"testing"
)

func TestValidateOCICreateZoneRegions(t *testing.T) {
	const (
		region    = "region-name=us-sanjose-1::vnet=ocid1.vcn.oc1..example"
		region2   = "region-name=us-phoenix-1::vnet=ocid1.vcn.oc1..phx"
		matching  = "zone-name=us-sanjose-1-AD-1::region-name=us-sanjose-1::subnet=ocid1.subnet.oc1..example"
		matching2 = "zone-name=us-phoenix-1-AD-1::region-name=us-phoenix-1::subnet=ocid1.subnet.oc1..phx"
		typoZone  = "zone-name=us-sanjose-1-AD-typo::region-name=us-sanjose-1-typo::subnet=ocid1.subnet.oc1..typo"
		typoZone2 = "zone-name=us-phoenix-1-AD-typo::region-name=us-phoenix-1-typo::subnet=ocid1.subnet.oc1..typo2"
	)
	tests := []struct {
		name    string
		regions []string
		zones   []string
		wantErr string
	}{
		{
			name:    "zone region matches --region",
			regions: []string{region},
			zones:   []string{matching},
		},
		{
			name:    "each zone matches its own region",
			regions: []string{region, region2},
			zones:   []string{matching, matching2},
		},
		{
			name:    "typo region on an extra zone",
			regions: []string{region},
			zones:   []string{matching, typoZone},
			wantErr: "region us-sanjose-1-typo in --zone matches no --region",
		},
		{
			name:    "repeated typo is named once",
			regions: []string{region},
			zones:   []string{typoZone, typoZone},
			wantErr: "region us-sanjose-1-typo in --zone matches no --region",
		},
		{
			name:    "every unknown region is named",
			regions: []string{region},
			zones:   []string{matching, typoZone, typoZone2},
			wantErr: "region us-sanjose-1-typo, us-phoenix-1-typo in --zone matches no --region",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := validateOCICreateZoneRegions(tc.regions, tc.zones)
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if err == nil || err.Error() != tc.wantErr {
				t.Fatalf("got error %v, want %q", err, tc.wantErr)
			}
		})
	}
}

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
