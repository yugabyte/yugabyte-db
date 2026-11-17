package providerutil

import (
	"os"
	"os/exec"
	"strings"
	"testing"
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
