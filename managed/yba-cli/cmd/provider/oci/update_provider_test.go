/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestValidateOCIRegionAndZoneNames(t *testing.T) {
	region := func(code string, zones ...string) ybaclient.Region {
		azs := make([]ybaclient.AvailabilityZone, 0, len(zones))
		for _, z := range zones {
			azs = append(azs, ybaclient.AvailabilityZone{Code: util.GetStringPointer(z), Name: z})
		}
		return ybaclient.Region{Code: util.GetStringPointer(code), Zones: azs}
	}
	providerRegions := []ybaclient.Region{
		region("r1", "r1-z1"),
		region("r2", "r2-z1"),
	}

	tests := []struct {
		name          string
		addRegions    []string
		removeRegions []string
		editRegions   []string
		addZones      []string
		editZones     []string
		removeZones   []string
		wantErr       string
	}{
		{
			name:          "all names exist",
			addRegions:    []string{"region-name=r3::vnet=v"},
			removeRegions: []string{"r2"},
			editRegions:   []string{"region-name=r1::vnet=v"},
			addZones: []string{
				"zone-name=r1-z2::region-name=r1::subnet=s",
				"zone-name=r3-z1::region-name=r3::subnet=s",
			},
			editZones:   []string{"zone-name=r1-z1::region-name=r1::subnet=s"},
			removeZones: []string{"zone-name=r2-z1::region-name=r2"},
		},
		{
			name:          "unknown region in remove-region",
			removeRegions: []string{"r1-typo"},
			wantErr:       "region r1-typo not found",
		},
		{
			name:        "unknown region in edit-region",
			editRegions: []string{"region-name=r1-typo::vnet=v"},
			wantErr:     "region r1-typo not found",
		},
		{
			name:        "edit-region clearing the instance template",
			editRegions: []string{"region-name=r1::instance-template="},
		},
		{
			name:        "unknown region in edit-region clearing the instance template",
			editRegions: []string{"region-name=r1-typo::instance-template="},
			wantErr:     "region r1-typo not found",
		},
		{
			name:     "unknown region in add-zone",
			addZones: []string{"zone-name=z::region-name=r1-typo::subnet=s"},
			wantErr:  "region r1-typo in --add-zone",
		},
		{
			name:      "unknown region in edit-zone",
			editZones: []string{"zone-name=r1-z1::region-name=r1-typo::subnet=s"},
			wantErr:   "region r1-typo not found",
		},
		{
			name:        "unknown zone in remove-zone",
			removeZones: []string{"zone-name=r1-z1-typo::region-name=r1"},
			wantErr:     "zone r1-z1-typo not found in region r1",
		},
		{
			name:      "zone from another region in edit-zone",
			editZones: []string{"zone-name=r2-z1::region-name=r1::subnet=s"},
			wantErr:   "zone r2-z1 not found in region r1",
		},
		{
			name:        "zone from another region in remove-zone",
			removeZones: []string{"zone-name=r2-z1::region-name=r1"},
			wantErr:     "zone r2-z1 not found in region r1",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := validateOCIRegionAndZoneNames(
				tc.addRegions, tc.removeRegions, tc.editRegions,
				tc.addZones, tc.editZones, tc.removeZones, providerRegions)
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("got error %v, want one containing %q", err, tc.wantErr)
			}
		})
	}
}

func ociZone(code string, active bool) ybaclient.AvailabilityZone {
	zone := ybaclient.AvailabilityZone{Code: util.GetStringPointer(code), Name: code}
	zone.SetActive(active)
	return zone
}

func ociRegion(code string, active bool, zones ...ybaclient.AvailabilityZone) ybaclient.Region {
	region := ybaclient.Region{Code: util.GetStringPointer(code), Zones: zones}
	region.SetActive(active)
	return region
}

func TestValidateOCIZoneRemovalLeavesActiveZone(t *testing.T) {
	const (
		regionName = "us-sanjose-1"
		onlyZone   = "zone-name=us-sanjose-1-AD-1::region-name=us-sanjose-1"
		otherZone  = "zone-name=us-sanjose-1-AD-2::region-name=us-sanjose-1"
		addedZone  = "zone-name=us-sanjose-1-AD-3::region-name=us-sanjose-1::subnet=s"
	)
	tests := []struct {
		name          string
		regions       []ybaclient.Region
		addZones      []string
		removeZones   []string
		removeRegions []string
		wantErr       string
	}{
		{
			name: "removing the only zone of a region",
			regions: []ybaclient.Region{ociRegion(regionName, true,
				ociZone("us-sanjose-1-AD-1", true))},
			removeZones: []string{onlyZone},
			wantErr:     "region us-sanjose-1 would have no active zone",
		},
		{
			name: "removing every zone of a region",
			regions: []ybaclient.Region{ociRegion(regionName, true,
				ociZone("us-sanjose-1-AD-1", true),
				ociZone("us-sanjose-1-AD-2", true))},
			removeZones: []string{onlyZone, otherZone},
			wantErr:     "remove the region with --remove-region",
		},
		{
			name: "removing the last active zone when another is already inactive",
			regions: []ybaclient.Region{ociRegion(regionName, true,
				ociZone("us-sanjose-1-AD-1", true),
				ociZone("us-sanjose-1-AD-2", false))},
			removeZones: []string{onlyZone},
			wantErr:     "region us-sanjose-1 would have no active zone",
		},
		{
			name: "removing one zone when another stays active",
			regions: []ybaclient.Region{ociRegion(regionName, true,
				ociZone("us-sanjose-1-AD-1", true),
				ociZone("us-sanjose-1-AD-2", true))},
			removeZones: []string{onlyZone},
		},
		{
			name: "removing the only zone while adding a replacement",
			regions: []ybaclient.Region{ociRegion(regionName, true,
				ociZone("us-sanjose-1-AD-1", true))},
			addZones:    []string{addedZone},
			removeZones: []string{onlyZone},
		},
		{
			name: "removing every zone of a region that is itself removed",
			regions: []ybaclient.Region{ociRegion(regionName, true,
				ociZone("us-sanjose-1-AD-1", true))},
			removeRegions: []string{regionName},
			removeZones:   []string{onlyZone},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			updated := removeOCIRegions(tc.removeRegions, tc.regions)
			updated = editOCIRegions(nil, tc.addZones, nil, tc.removeZones, updated)
			err := providerutil.ValidateZoneRemovalLeavesActiveZone(tc.removeZones, updated)
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("got error %v, want one containing %q", err, tc.wantErr)
			}
		})
	}
}

func TestEditOCIRegionsInstanceTemplate(t *testing.T) {
	const oldTemplate = "ocid1.instanceconfiguration.oc1..old"
	tests := []struct {
		name       string
		editRegion string
		want       string
	}{
		{
			name:       "empty value clears the template",
			editRegion: "region-name=r1::instance-template=",
			want:       "",
		},
		{
			name:       "omitted key keeps the template",
			editRegion: "region-name=r1::vnet=v2",
			want:       oldTemplate,
		},
		{
			name:       "new value replaces the template",
			editRegion: "region-name=r1::instance-template=ocid1.instanceconfiguration.oc1..new",
			want:       "ocid1.instanceconfiguration.oc1..new",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			providerRegions := []ybaclient.Region{{
				Code: util.GetStringPointer("r1"),
				Zones: []ybaclient.AvailabilityZone{
					{Code: util.GetStringPointer("r1-z1"), Name: "r1-z1"},
				},
				Details: &ybaclient.RegionDetails{
					CloudInfo: &ybaclient.RegionCloudInfo{
						Oci: &ybaclient.OCIRegionCloudInfo{
							Vnet:             util.GetStringPointer("v1"),
							InstanceTemplate: util.GetStringPointer(oldTemplate),
						},
					},
				},
			}}

			got := editOCIRegions([]string{tc.editRegion}, nil, nil, nil, providerRegions)

			oci := got[0].GetDetails().CloudInfo.GetOci()
			template, ok := oci.GetInstanceTemplateOk()
			if !ok || *template != tc.want {
				t.Fatalf("instance template = %v (set=%v), want %q", template, ok, tc.want)
			}
		})
	}
}
