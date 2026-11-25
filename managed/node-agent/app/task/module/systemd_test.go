// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"testing"
)

func TestDuplicateSystemdUnit(t *testing.T) {
	const service = "yb-node-agent.service"
	const systemUnit = "/etc/systemd/system/" + service
	const userCgroup = "0::/user.slice/user-1000.slice/user@1000.service/app.slice/" + service
	const systemCgroup = "0::/system.slice/" + service
	originalShow, originalCgroup, originalUserDir := ShowSystemUnitState, SelfCgroupPath,
		UserSystemdUnitDir
	t.Cleanup(func() {
		ShowSystemUnitState, SelfCgroupPath, UserSystemdUnitDir = originalShow, originalCgroup,
			originalUserDir
	})
	SelfCgroupPath = filepath.Join(t.TempDir(), "cgroup")

	tests := []struct {
		name            string
		cgroup          string
		state           SystemUnitState
		userUnitEnabled bool
		want            string
	}{
		{"system unit looping", userCgroup,
			SystemUnitState{"loaded", "disabled", "activating", systemUnit}, false, systemUnit},
		{"system unit enabled but stopped", userCgroup,
			SystemUnitState{"loaded", "enabled", "inactive", systemUnit}, false, systemUnit},
		{"system unit left disabled", userCgroup,
			SystemUnitState{"loaded", "disabled", "inactive", systemUnit}, false, ""},
		{"system unit masked", userCgroup,
			SystemUnitState{"masked", "masked", "inactive", "/dev/null"}, false, ""},
		{"no system unit", userCgroup,
			SystemUnitState{"not-found", "", "inactive", ""}, false, ""},
		// The live agent is the system unit; a leftover user unit file must not block upgrades.
		{"running as system unit", systemCgroup,
			SystemUnitState{"loaded", "enabled", "active", systemUnit}, false, ""},
		// The system unit won the server lock and the user unit waits next to it.
		{"running as system unit next to enabled user unit", systemCgroup,
			SystemUnitState{"loaded", "enabled", "active", systemUnit}, true, systemUnit},
		// Run by hand, not by a unit.
		{"not run by a unit", "0::/user.slice/user-1000.slice/session-3.scope",
			SystemUnitState{"loaded", "enabled", "active", systemUnit}, true, ""},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if err := os.WriteFile(SelfCgroupPath, []byte(tc.cgroup+"\n"), 0644); err != nil {
				t.Fatal(err)
			}
			userDir := t.TempDir()
			UserSystemdUnitDir = func() (string, error) { return userDir, nil }
			// A user unit file is there in every case; enabling it adds the .wants link.
			if err := os.WriteFile(filepath.Join(userDir, service), nil, 0644); err != nil {
				t.Fatal(err)
			}
			if tc.userUnitEnabled {
				wants := filepath.Join(userDir, "default.target.wants")
				if err := os.Mkdir(wants, 0755); err != nil {
					t.Fatal(err)
				}
				err := os.Symlink(filepath.Join(userDir, service), filepath.Join(wants, service))
				if err != nil {
					t.Fatal(err)
				}
			}
			ShowSystemUnitState = func(context.Context, string) (SystemUnitState, error) {
				return tc.state, nil
			}
			got, err := DuplicateSystemdUnit(context.Background(), service)
			if err != nil {
				t.Fatal(err)
			}
			if got != tc.want {
				t.Fatalf("got %q, want %q", got, tc.want)
			}
		})
	}
	t.Run("systemctl error", func(t *testing.T) {
		os.WriteFile(SelfCgroupPath, []byte(userCgroup+"\n"), 0644)
		ShowSystemUnitState = func(context.Context, string) (SystemUnitState, error) {
			return SystemUnitState{}, errors.New("no systemd")
		}
		if _, err := DuplicateSystemdUnit(context.Background(), service); err == nil {
			t.Fatal("expected error")
		}
	})
}

func TestParseSystemdProperties(t *testing.T) {
	got := parseSystemdProperties("LoadState=loaded\nFragmentPath=/a=b\n\nnoise\n")
	if got["LoadState"] != "loaded" || got["FragmentPath"] != "/a=b" || len(got) != 2 {
		t.Fatalf("unexpected %v", got)
	}
}
