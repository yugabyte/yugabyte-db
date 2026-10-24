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

// The disable path calls configureOtelCollector without the package setup, on nodes that may
// never have had the collector, so it must work when <yb_home>/otel-collector does not exist.
func TestConfigureOtelCollectorCreatesMissingDir(t *testing.T) {
	currentUser, err := user.Current()
	if err != nil {
		t.Fatalf("current user: %v", err)
	}
	ybHome := t.TempDir()
	handler := NewInstallOtelCollectorHandler(
		&pb.InstallOtelCollectorInput{
			YbHomeDir:         ybHome,
			YcqlAuditLogLevel: "NONE",
			DisableCollector:  true,
		},
		currentUser.Username,
	)

	if err := handler.configureOtelCollector(context.Background(), ybHome); err != nil {
		t.Fatalf("configureOtelCollector: %v", err)
	}

	otelDir := filepath.Join(ybHome, "otel-collector")
	if _, err := os.Stat(filepath.Join(otelDir, "log_cleanup_env")); err != nil {
		t.Fatalf("log_cleanup_env was not written: %v", err)
	}
	if _, err := os.Stat(filepath.Join(otelDir, "config.yml")); !os.IsNotExist(err) {
		t.Fatalf("config.yml should not exist, got err=%v", err)
	}
}
