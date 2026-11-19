package providerutil

import (
	"os"
	"os/exec"
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

const fatalCaseEnv = "TEST_REGION_MAP_FATAL_CASE"

func TestBuildRegionMapInstanceTemplate(t *testing.T) {
	cases := []struct {
		name        string
		allowClear  bool
		region      string
		operation   string
		wantFatal   bool
		wantPresent bool
		wantValue   string
	}{
		{
			name:        "edit keeps empty value as clear",
			allowClear:  true,
			region:      "region-name=r1::vnet=v1::instance-template=",
			operation:   "edit",
			wantPresent: true,
			wantValue:   "",
		},
		{
			name:       "edit leaves missing key absent",
			allowClear: true,
			region:     "region-name=r1::vnet=v1",
			operation:  "edit",
		},
		{
			name:       "add rejects empty value",
			allowClear: true,
			region:     "region-name=r1::vnet=v1::instance-template=",
			operation:  "add",
			wantFatal:  true,
		},
		{
			name:      "edit rejects empty value for providers that cannot clear it",
			region:    "region-name=r1::instance-template=",
			operation: "edit",
			wantFatal: true,
		},
	}

	build := func(allowClear bool, region, operation string) map[string]string {
		if allowClear {
			return BuildRegionMapFromStringAllowingClearInstanceTemplate(region, operation)
		}
		return BuildRegionMapFromString(region, operation)
	}

	// ValueNotFoundForKeyError exits the process, so fatal cases run in a child.
	if name := os.Getenv(fatalCaseEnv); name != "" {
		for _, c := range cases {
			if c.name == name {
				build(c.allowClear, c.region, c.operation)
				return
			}
		}
		t.Fatalf("unknown case %q", name)
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if c.wantFatal {
				cmd := exec.Command(os.Args[0], "-test.run=^TestBuildRegionMapInstanceTemplate$")
				cmd.Env = append(os.Environ(), fatalCaseEnv+"="+c.name)
				if err := cmd.Run(); err == nil {
					t.Fatal("expected the parser to exit with an error")
				}
				return
			}
			got, ok := build(c.allowClear, c.region, c.operation)["instance-template"]
			if ok != c.wantPresent || got != c.wantValue {
				t.Fatalf("instance-template = %q (present=%v), want %q (present=%v)",
					got, ok, c.wantValue, c.wantPresent)
			}
		})
	}
}

func TestValidateZoneRemovalLeavesActiveZone(t *testing.T) {
	active := true
	inactive := false
	zone := func(code string, setActive bool, value bool) ybaclient.AvailabilityZone {
		z := ybaclient.AvailabilityZone{Code: util.GetStringPointer(code), Name: code}
		if setActive {
			z.SetActive(value)
		}
		return z
	}
	region := func(code string, regionActive bool, zones ...ybaclient.AvailabilityZone) ybaclient.Region {
		r := ybaclient.Region{Code: util.GetStringPointer(code), Zones: zones}
		r.SetActive(regionActive)
		return r
	}
	remove := []string{"zone-name=z1::region-name=r1"}
	tests := []struct {
		name        string
		removeZones []string
		regions     []ybaclient.Region
		wantErr     string
	}{
		{
			name: "no removal",
			regions: []ybaclient.Region{
				region("r1", true, zone("z1", true, false)),
			},
		},
		{
			name:        "only zone inactive",
			removeZones: remove,
			regions: []ybaclient.Region{
				region("r1", true, zone("z1", true, false)),
			},
			wantErr: "region r1 would have no active zone",
		},
		{
			name:        "omitted zone active flag counts as active",
			removeZones: remove,
			regions: []ybaclient.Region{
				region("r1", true, zone("z1", true, false), zone("z2", false, active)),
			},
		},
		{
			name:        "inactive region is skipped",
			removeZones: remove,
			regions: []ybaclient.Region{
				region("r1", false, zone("z1", true, inactive)),
			},
		},
		{
			name:        "another region is not checked",
			removeZones: remove,
			regions: []ybaclient.Region{
				region("r1", true, zone("z1", true, true)),
				region("r2", true, zone("z2", true, false)),
			},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ValidateZoneRemovalLeavesActiveZone(tc.removeZones, tc.regions)
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
