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
	originalShow, originalCgroup := ShowSystemUnitState, SelfCgroupPath
	t.Cleanup(func() { ShowSystemUnitState, SelfCgroupPath = originalShow, originalCgroup })
	SelfCgroupPath = filepath.Join(t.TempDir(), "cgroup")

	tests := []struct {
		name   string
		cgroup string
		state  SystemUnitState
		want   string
	}{
		{"system unit looping", userCgroup,
			SystemUnitState{"loaded", "disabled", "activating", systemUnit}, systemUnit},
		{"system unit enabled but stopped", userCgroup,
			SystemUnitState{"loaded", "enabled", "inactive", systemUnit}, systemUnit},
		{"system unit left disabled", userCgroup,
			SystemUnitState{"loaded", "disabled", "inactive", systemUnit}, ""},
		{"system unit masked", userCgroup,
			SystemUnitState{"masked", "masked", "inactive", "/dev/null"}, ""},
		{"no system unit", userCgroup,
			SystemUnitState{"not-found", "", "inactive", ""}, ""},
		// The live agent is the system unit; a leftover user unit file must not block upgrades.
		{"running as system unit", systemCgroup,
			SystemUnitState{"loaded", "enabled", "active", systemUnit}, ""},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if err := os.WriteFile(SelfCgroupPath, []byte(tc.cgroup+"\n"), 0644); err != nil {
				t.Fatal(err)
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
