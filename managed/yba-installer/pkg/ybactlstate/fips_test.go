package ybactlstate

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

// reconfigState returns a state that ValidateReconfig accepts against the viper values it sets.
func reconfigState(t *testing.T, fipsEnabled bool) State {
	t.Cleanup(viper.Reset)
	viper.Set("installRoot", "/opt/yugabyte")
	viper.Set("service_username", "yugabyte")
	viper.Set("prometheus.scrapeTimeout", "10s")
	viper.Set("prometheus.scrapeInterval", "15s")
	viper.Set("fips.enabled", fipsEnabled)
	return State{
		RootInstall: "/opt/yugabyte",
		Username:    "yugabyte",
		Config:      Config{FipsEnabled: fipsEnabled},
	}
}

func TestValidateReconfigRejectsFipsChange(t *testing.T) {
	for _, installed := range []bool{true, false} {
		state := reconfigState(t, installed)
		if err := state.ValidateReconfig(); err != nil {
			t.Fatalf("unchanged fips.enabled=%t rejected: %v", installed, err)
		}
		viper.Set("fips.enabled", !installed)
		err := state.ValidateReconfig()
		if err == nil || !strings.Contains(err.Error(), "cannot change fips.enabled") {
			t.Fatalf("changing fips.enabled from %t was not rejected: %v", installed, err)
		}
	}
}

func TestMigrateFipsStateReadsInstalledUnit(t *testing.T) {
	oldDir := common.SystemdDir
	t.Cleanup(func() { common.SystemdDir = oldDir; viper.Reset() })
	common.SystemdDir = t.TempDir()
	unit := filepath.Join(common.SystemdDir, "yb-platform.service")

	// yba-ctl.yml already says the opposite: the installed unit must win.
	cases := map[string]bool{
		"ExecStart=/bin/java \\\n  -Dorg.bouncycastle.fips.approved_only=true \\\n": true,
		"ExecStart=/bin/java \\\n": false,
	}
	for contents, want := range cases {
		viper.Set("fips.enabled", !want)
		if err := os.WriteFile(unit, []byte(contents), 0644); err != nil {
			t.Fatal(err)
		}
		state := &State{}
		if err := migrateFipsState(state); err != nil {
			t.Fatal(err)
		}
		if state.Config.FipsEnabled != want {
			t.Fatalf("unit %q: FipsEnabled = %t, want %t", contents, state.Config.FipsEnabled, want)
		}
	}

	// Not installed yet: fall back to the configured value.
	os.Remove(unit)
	viper.Set("fips.enabled", true)
	state := &State{}
	if err := migrateFipsState(state); err != nil {
		t.Fatal(err)
	}
	if !state.Config.FipsEnabled {
		t.Fatal("missing unit did not fall back to fips.enabled")
	}
}

func TestValidateReinstallKeepsSoftCleanedFipsMode(t *testing.T) {
	t.Cleanup(viper.Reset)
	softCleaned := State{CurrentStatus: SoftCleanStatus, Config: Config{FipsEnabled: true}}

	viper.Set("fips.enabled", true)
	if err := softCleaned.ValidateReinstall(); err != nil {
		t.Fatalf("same mode over kept data rejected: %v", err)
	}
	viper.Set("fips.enabled", false)
	err := softCleaned.ValidateReinstall()
	if err == nil || !strings.Contains(err.Error(), "yba-ctl clean --all") {
		t.Fatalf("mode change over kept data was not rejected: %v", err)
	}

	// clean --all removes the state file, so install starts from New() and may pick either mode.
	fresh := New()
	if err := fresh.ValidateReinstall(); err != nil {
		t.Fatalf("fresh install rejected: %v", err)
	}
	if fresh.Config.FipsEnabled {
		t.Fatal("fresh state did not take fips.enabled from the config")
	}
}
