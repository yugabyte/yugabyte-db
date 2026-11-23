/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestEditGCPRegionsInstanceTemplate(t *testing.T) {
	const oldTemplate = "old-template"
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
			editRegion: "region-name=r1::shared-subnet=s2",
			want:       oldTemplate,
		},
		{
			name:       "new value replaces the template",
			editRegion: "region-name=r1::instance-template=new-template",
			want:       "new-template",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			providerRegions := []ybaclient.Region{{
				Code: util.GetStringPointer("r1"),
				Zones: []ybaclient.AvailabilityZone{
					{Code: util.GetStringPointer("r1-a"), Name: "r1-a"},
				},
				Details: &ybaclient.RegionDetails{
					CloudInfo: &ybaclient.RegionCloudInfo{
						Gcp: &ybaclient.GCPRegionCloudInfo{
							InstanceTemplate: util.GetStringPointer(oldTemplate),
						},
					},
				},
			}}

			got := editGCPRegions([]string{tc.editRegion}, providerRegions)

			gcp := got[0].GetDetails().CloudInfo.GetGcp()
			template, ok := gcp.GetInstanceTemplateOk()
			if !ok || *template != tc.want {
				t.Fatalf("instance template = %v (set=%v), want %q", template, ok, tc.want)
			}
		})
	}
}
