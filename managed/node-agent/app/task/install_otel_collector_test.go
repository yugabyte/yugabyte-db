// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	pb "node-agent/generated/service"
	"os"
	"os/user"
	"path/filepath"
	"testing"
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
	if unit, err := handler.installedOtelCollectorUnit(); err != nil || unit != "" {
		t.Skipf("this host has a user-level otel-collector unit (%q, %v)", unit, err)
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

// A root-systemd node has its unit in the system unit directory, and its sudoers rules match the
// unit name without the .service suffix.
func TestInstalledOtelCollectorUnitFindsRootUnit(t *testing.T) {
	handler := newDisableCollectorHandler(t, t.TempDir())
	unit, err := handler.installedOtelCollectorUnit()
	if err != nil {
		t.Fatalf("installedOtelCollectorUnit: %v", err)
	}
	if unit == OtelCollectorService {
		t.Skip("this host has a user-level otel-collector unit, which takes precedence")
	}
	if unit != "" {
		t.Fatalf("no root unit yet: got %q", unit)
	}
	if err := os.WriteFile(handler.rootUnitPath, []byte("[Unit]\n"), 0644); err != nil {
		t.Fatalf("write unit: %v", err)
	}
	unit, err = handler.installedOtelCollectorUnit()
	if err != nil {
		t.Fatalf("installedOtelCollectorUnit: %v", err)
	}
	if unit != otelCollectorRootUnitName {
		t.Fatalf("got unit %q, want %q", unit, otelCollectorRootUnitName)
	}
}
