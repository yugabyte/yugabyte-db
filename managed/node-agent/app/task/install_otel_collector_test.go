// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"os"
	"os/user"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// newDisableCollectorHandler points the root unit path at a test directory, so the host's own
// systemd units do not decide the outcome.
func newDisableCollectorHandler(t *testing.T, ybHome string) *InstallOtelCollector {
	currentUser, err := user.Current()
	if err != nil {
		t.Fatalf("current user: %v", err)
	}
	handler := NewInstallOtelCollectorHandler(
		&pb.InstallOtelCollectorInput{
			YbHomeDir:         ybHome,
			YcqlAuditLogLevel: "NONE",
			DisableCollector:  true,
		},
		currentUser.Username,
	)
	handler.rootUnitPath = filepath.Join(t.TempDir(), OtelCollectorService)
	return handler
}

// The disable path runs without the package setup, on nodes that may never have had the
// collector: no unit to stop and no <yb_home>/otel-collector directory.
func TestDisableOtelCollectorWithoutUnitOrDir(t *testing.T) {
	ybHome := t.TempDir()
	handler := newDisableCollectorHandler(t, ybHome)
	hasUser, _, err := module.IsUserSystemd(handler.username, OtelCollectorService)
	if err != nil {
		t.Fatalf("IsUserSystemd: %v", err)
	}
	if hasUser {
		t.Skip("this host has a user-level otel-collector unit")
	}

	if err := handler.disableOtelCollector(context.Background(), ybHome); err != nil {
		t.Fatalf("disableOtelCollector: %v", err)
	}

	otelDir := filepath.Join(ybHome, "otel-collector")
	if _, err := os.Stat(filepath.Join(otelDir, "log_cleanup_env")); err != nil {
		t.Fatalf("log_cleanup_env was not written: %v", err)
	}
	if _, err := os.Stat(filepath.Join(otelDir, "config.yml")); !os.IsNotExist(err) {
		t.Fatalf("config.yml should not exist, got err=%v", err)
	}
}

// A root-systemd node has its unit in the system unit directory.
func TestHasRootOtelCollectorUnit(t *testing.T) {
	handler := newDisableCollectorHandler(t, t.TempDir())
	if hasRoot, err := handler.hasRootOtelCollectorUnit(); err != nil || hasRoot {
		t.Fatalf("no root unit yet: hasRoot=%v, err=%v", hasRoot, err)
	}
	if err := os.WriteFile(handler.rootUnitPath, []byte("[Unit]\n"), 0644); err != nil {
		t.Fatalf("write unit: %v", err)
	}
	if hasRoot, err := handler.hasRootOtelCollectorUnit(); err != nil || !hasRoot {
		t.Fatalf("root unit not found: hasRoot=%v, err=%v", hasRoot, err)
	}
}

func TestRootOtelCollectorInUse(t *testing.T) {
	tests := []struct {
		active, enabled string
		want            bool
	}{
		{"active", "enabled", true},
		{"inactive", "enabled", true},
		{"inactive", "linked", true},
		{"inactive", "alias", true},
		{"activating", "disabled", true},
		{"inactive", "disabled", false},
		{"failed", "disabled", false},
		{"unknown", "static", false},
	}
	for _, tt := range tests {
		if got := rootOtelCollectorInUse(tt.active, tt.enabled); got != tt.want {
			t.Errorf("rootOtelCollectorInUse(%q, %q) = %v, want %v",
				tt.active, tt.enabled, got, tt.want)
		}
	}
}

func TestOtelCollectorStillRunningError(t *testing.T) {
	binary := "/home/yugabyte/otel-collector/otelcol-contrib"
	tests := []struct {
		rootStopFailed bool
		wantCause      string
	}{
		{true, "root-level"},
		{false, "user systemd manager"},
	}
	for _, tt := range tests {
		msg := otelCollectorStillRunningError(binary, tt.rootStopFailed).Error()
		if !strings.Contains(msg, binary) || !strings.Contains(msg, tt.wantCause) {
			t.Errorf("rootStopFailed=%v: error %q must name %s and %q",
				tt.rootStopFailed, msg, binary, tt.wantCause)
		}
	}
}

// A crash-looping collector runs only part of each restart cycle, so a single check can miss it.
func TestOtelCollectorRunsWithin(t *testing.T) {
	calls := 0
	running, err := otelCollectorRunsWithin(
		context.Background(),
		time.Second,
		time.Millisecond,
		func() (bool, error) {
			calls++
			return calls == 3, nil
		},
	)
	if err != nil || !running {
		t.Fatalf("collector seen on call 3 not found: running=%v, err=%v", running, err)
	}
	running, err = otelCollectorRunsWithin(
		context.Background(),
		20*time.Millisecond,
		time.Millisecond,
		func() (bool, error) { return false, nil },
	)
	if err != nil || running {
		t.Fatalf("no collector: running=%v, err=%v", running, err)
	}
}

// The link removal must do what systemctl --user disable does and keep the unit file.
func TestRemoveOtelCollectorWantsLink(t *testing.T) {
	home := t.TempDir()
	unitDir := filepath.Join(home, ".config/systemd/user")
	wantsDir := filepath.Join(unitDir, "default.target.wants")
	if err := os.MkdirAll(wantsDir, 0755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	unit := filepath.Join(unitDir, OtelCollectorService)
	if err := os.WriteFile(unit, []byte("[Unit]\n"), 0644); err != nil {
		t.Fatalf("write unit: %v", err)
	}
	link := filepath.Join(wantsDir, OtelCollectorService)
	if err := os.Symlink(unit, link); err != nil {
		t.Fatalf("symlink: %v", err)
	}
	handler := newDisableCollectorHandler(t, t.TempDir())
	if err := handler.removeOtelCollectorWantsLink(context.Background(), home); err != nil {
		t.Fatalf("removeOtelCollectorWantsLink: %v", err)
	}
	if _, err := os.Lstat(link); !os.IsNotExist(err) {
		t.Fatalf("link should be gone, got err=%v", err)
	}
	if _, err := os.Stat(unit); err != nil {
		t.Fatalf("unit file should stay: %v", err)
	}
}
