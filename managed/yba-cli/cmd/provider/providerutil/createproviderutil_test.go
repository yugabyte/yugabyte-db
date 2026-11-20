/*
 * Copyright (c) YugabyteDB, Inc.
 */

package providerutil

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestValidateCreateZoneRegions(t *testing.T) {
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
		{
			name:    "zone without any --region",
			zones:   []string{matching},
			wantErr: "region us-sanjose-1 in --zone matches no --region",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ValidateCreateZoneRegions(tc.regions, tc.zones)
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

func TestValidateUpdateAddZoneRegions(t *testing.T) {
	providerRegions := []ybaclient.Region{{Code: util.GetStringPointer("r1")}}
	tests := []struct {
		name       string
		addRegions []string
		addZones   []string
		wantErr    string
	}{
		{name: "no zones"},
		{
			name:     "zone in an existing region",
			addZones: []string{"zone-name=z::region-name=r1"},
		},
		{
			name:       "zone in an added region",
			addRegions: []string{"region-name=r2"},
			addZones:   []string{"zone-name=z::region-name=r2"},
		},
		{
			name:     "zone in an unknown region",
			addZones: []string{"zone-name=z::region-name=typo"},
			wantErr:  "region typo in --add-zone is not in the provider or in --add-region",
		},
		{
			name: "every unknown region is named once",
			addZones: []string{
				"zone-name=z1::region-name=typo",
				"zone-name=z2::region-name=r1",
				"zone-name=z3::region-name=typo2",
				"zone-name=z4::region-name=typo",
			},
			wantErr: "region typo, typo2 in --add-zone is not in the provider or in --add-region",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ValidateUpdateAddZoneRegions(tc.addRegions, tc.addZones, providerRegions)
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
