package providerutil

import (
	"os"
	"os/exec"
	"testing"
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
