package integrationtests

import (
	"encoding/json"
	"testing"

	"github.com/goccy/go-yaml"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/containertest"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

func installAndValidateYBA(t *testing.T, mgr containertest.Manager, ctrRef containertest.ContainerRef) {
	t.Helper()
	// Install YBA
	if output := mgr.Exec(ctrRef, "/yba_installer/yba-ctl", "install", "-f", "-l", "/yba.lic"); !output.Succeeded() {
		t.Fatalf("failed to install on container %s YBA: %v", ctrRef.ContainerName, output.StderrString())
	}

	// Validate YBA installation
	if output := mgr.Exec(ctrRef, "/yba_installer/yba-ctl", "status"); !output.Succeeded() {
		t.Fatalf("YBA validation failed: %v", output.StderrString())
	}
}

func getYbaInstallerConfig(tb testing.TB, mgr containertest.Manager, ctrRef containertest.ContainerRef) map[string]any {
	tb.Helper()
	output := mgr.Exec(ctrRef, "cat", "/opt/yba-ctl/yba-ctl.yml")
	if !output.Succeeded() {
		tb.Fatalf("failed to get yba-ctl config: %v", output.StderrString())
	}
	config := make(map[string]any)
	if err := yaml.Unmarshal(output.StdoutBytes(), &config); err != nil {
		tb.Fatalf("failed to unmarshal yba-ctl config: %v", err)
	}
	return config
}

func getYbaInstallerStatefile(tb testing.TB, mgr containertest.Manager, ctrRef containertest.ContainerRef) ybactlstate.State {
	tb.Helper()
	output := mgr.Exec(ctrRef, "cat", "/opt/yba-ctl/.yba_installer.state")
	if !output.Succeeded() {
		tb.Fatalf("failed to get yba-ctl statefile: %v", output.StderrString())
	}
	var state ybactlstate.State
	if err := json.Unmarshal(output.StdoutBytes(), &state); err != nil {
		tb.Fatalf("failed to unmarshal yba-ctl statefile: %v", err)
	}
	return state
}

func copyCertsAndUpdateConfig(tb testing.TB, mgr containertest.Manager, ctrRef containertest.ContainerRef, certPath, keyPath string) {
	tb.Helper()
	// Create certs directory in container
	if output := mgr.Exec(ctrRef, "mkdir", "-p", "/certs"); !output.Succeeded() {
		tb.Fatalf("failed to create /certs directory: %v", output.StderrString())
	}

	// Copy certs to container
	if err := mgr.CopyTo(ctrRef, certPath, "/certs/server_cert.pem"); err != nil {
		tb.Fatalf("failed to copy server_cert.pem: %v", err)
	}
	if err := mgr.CopyTo(ctrRef, keyPath, "/certs/server_key.pem"); err != nil {
		tb.Fatalf("failed to copy server_key.pem: %v", err)
	}

	// Update yba-ctl config with new cert paths
	if output := mgr.Exec(ctrRef, "sed", "-i", "s|server_cert_path:.*|server_cert_path: /certs/server_cert.pem|", "/opt/yba-ctl/yba-ctl.yml"); !output.Succeeded() {
		tb.Fatalf("failed to update server_cert_path in yba-ctl.yml: %v", output.StderrString())
	}
	if output := mgr.Exec(ctrRef, "sed", "-i", "s|server_key_path:.*|server_key_path: /certs/server_key.pem|", "/opt/yba-ctl/yba-ctl.yml"); !output.Succeeded() {
		tb.Fatalf("failed to update server_key_path in yba-ctl.yml: %v", output.StderrString())
	}
}
