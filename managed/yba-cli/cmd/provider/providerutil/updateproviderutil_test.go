package providerutil

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

const (
	fatalCaseEnv           = "TEST_REGION_MAP_FATAL_CASE"
	unknownKeyFatalCaseEnv = "TEST_UNKNOWN_KEY_FATAL_CASE"
)

func TestBuildRegionMapInstanceTemplate(t *testing.T) {
	cases := []struct {
		name        string
		allowClear  bool
		region      string
		operation   string
		wantFatal   string
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
			wantFatal:  `Key "instance-template" specified but value is empty`,
		},
		{
			name:      "edit rejects empty value for providers that cannot clear it",
			region:    "region-name=r1::instance-template=",
			operation: "edit",
			wantFatal: `Key "instance-template" specified but value is empty`,
		},
		{
			name:       "edit rejects an unknown key",
			allowClear: true,
			region:     "region-name=r1::vnet=v1::instance_template=t1",
			operation:  "edit",
			wantFatal:  `Unknown key "instance_template" in region description`,
		},
		{
			name:      "add rejects an unknown key",
			region:    "region-name=r1::vnet=v1::instance_template=t1",
			operation: "add",
			wantFatal: `Unknown key "instance_template" in region description`,
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
			if c.wantFatal != "" {
				cmd := exec.Command(os.Args[0], "-test.run=^TestBuildRegionMapInstanceTemplate$")
				cmd.Env = append(os.Environ(), fatalCaseEnv+"="+c.name)
				out, err := cmd.CombinedOutput()
				if err == nil {
					t.Fatalf("expected the parser to exit with an error, output %s", out)
				}
				if !strings.Contains(unescapeLog(out), c.wantFatal) {
					t.Fatalf("output %q, want %q", out, c.wantFatal)
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

func TestSSHPort(t *testing.T) {
	for _, port := range []int{1, 22, 65535} {
		if got, err := SSHPort(port); err != nil || got != int32(port) {
			t.Errorf("SSHPort(%d) = (%d, %v), want %d", port, got, err, port)
		}
	}
	for _, port := range []int{0, -1, 65536, 1 << 32} {
		want := fmt.Sprintf("invalid ssh-port %d; provide a port between 1 and 65535", port)
		if _, err := SSHPort(port); err == nil || err.Error() != want {
			t.Errorf("SSHPort(%d) error = %v, want %q", port, err, want)
		}
	}
}

func TestImageBundleSSHPort(t *testing.T) {
	tests := []struct {
		name    string
		bundle  map[string]string
		want    int32
		wantErr string
	}{
		{name: "omitted", bundle: map[string]string{"name": "b1"}, want: 22},
		{name: "valid", bundle: map[string]string{"name": "b1", "ssh-port": "2222"}, want: 2222},
		{name: "max", bundle: map[string]string{"name": "b1", "ssh-port": "65535"}, want: 65535},
		{
			name:    "zero",
			bundle:  map[string]string{"name": "b1", "ssh-port": "0"},
			wantErr: "invalid ssh-port 0 for image bundle b1; provide a port between 1 and 65535",
		},
		{
			name:    "above max",
			bundle:  map[string]string{"name": "b1", "ssh-port": "65536"},
			wantErr: "invalid ssh-port 65536 for image bundle b1; provide a port between 1 and 65535",
		},
		{
			name:    "not a number",
			bundle:  map[string]string{"name": "b1", "ssh-port": "abc"},
			wantErr: "invalid ssh-port abc for image bundle b1; provide a port between 1 and 65535",
		},
		{
			name:    "negative",
			bundle:  map[string]string{"name": "b1", "ssh-port": "-1"},
			wantErr: "invalid ssh-port -1 for image bundle b1; provide a port between 1 and 65535",
		},
		{
			name:    "edit identifies the bundle by uuid",
			bundle:  map[string]string{"uuid": "u1", "ssh-port": "70000"},
			wantErr: "invalid ssh-port 70000 for image bundle u1; provide a port between 1 and 65535",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ImageBundleSSHPort(tc.bundle)
			if tc.wantErr != "" {
				if err == nil || err.Error() != tc.wantErr {
					t.Fatalf("got (%d, %v), want error %q", got, err, tc.wantErr)
				}
				return
			}
			if err != nil || got != tc.want {
				t.Fatalf("got (%d, %v), want %d", got, err, tc.want)
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

func TestBuildMapsRejectUnknownKeys(t *testing.T) {
	cases := []struct {
		name    string
		valid   string
		typo    string
		typoKey string
		build   func(string) map[string]string
	}{
		{
			name:    "zone",
			valid:   "zone-name=z1::region-name=r1::secondary-subnet=s2::num-nodes=1",
			typo:    "zone-name=z1::region-name=r1::secondary_subnet=s2",
			typoKey: "secondary_subnet",
			build:   func(s string) map[string]string { return BuildZoneMapFromString(s, "edit") },
		},
		{
			name:    "image bundle",
			valid:   "image-bundle-name=b1::machine-image=i1::ssh-user=u1::ssh-port=22::imdsv2=true",
			typo:    "image-bundle-name=b1::ssh_user=u1",
			typoKey: "ssh_user",
			build:   func(s string) map[string]string { return BuildImageBundleMapFromString(s, "add") },
		},
		{
			name:    "image bundle region override",
			valid:   "image-bundle-name=b1::region-name=r1::machine-image=i1",
			typo:    "image-bundle-name=b1::region-name=r1::machine_image=i1",
			typoKey: "machine_image",
			build: func(s string) map[string]string {
				return BuildImageBundleRegionOverrideMapFromString(s, "add")
			},
		},
	}

	// UnknownKeyError exits the process, so the typo runs in a child.
	if name := os.Getenv(unknownKeyFatalCaseEnv); name != "" {
		for _, c := range cases {
			if c.name == name {
				c.build(c.typo)
				return
			}
		}
		t.Fatalf("unknown case %q", name)
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			c.build(c.valid)
			cmd := exec.Command(os.Args[0], "-test.run=^TestBuildMapsRejectUnknownKeys$")
			cmd.Env = append(os.Environ(), unknownKeyFatalCaseEnv+"="+c.name)
			out, err := cmd.CombinedOutput()
			if err == nil {
				t.Fatalf("expected the parser to exit with an error, output %s", out)
			}
			want := "Unknown key \"" + c.typoKey + "\" in " + c.name + " description"
			if !strings.Contains(unescapeLog(out), want) {
				t.Fatalf("output %q, want %q", out, want)
			}
		})
	}
}

// unescapeLog undoes the quoting logrus applies to msg when stderr is not a terminal.
func unescapeLog(out []byte) string {
	return strings.ReplaceAll(string(out), `\"`, `"`)
}

func TestBuildRegionMapCoordinates(t *testing.T) {
	region := BuildRegionMapFromString("region-name=r1::latitude=37.7::longitude=-122.4", "edit")
	if region["latitude"] != "37.7" || region["longitude"] != "-122.4" {
		t.Fatalf("latitude = %q, longitude = %q, want 37.7 and -122.4",
			region["latitude"], region["longitude"])
	}
}
