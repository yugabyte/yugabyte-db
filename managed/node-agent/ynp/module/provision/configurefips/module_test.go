// Copyright (c) YugabyteDB, Inc.

package configurefips

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

const pendingRebootFailure = `"FIPS Mode" "FAIL" \
          "FIPS mode is configured but not active until the node reboots`

func configureFipsModulesPath(t *testing.T) string {
	t.Helper()
	if projectDir := os.Getenv("PROJECT_DIR"); projectDir != "" {
		return filepath.Join(projectDir, "resources", "ynp", "modules", "provision")
	}
	path, err := filepath.Abs(
		filepath.Join("..", "..", "..", "..", "resources", "ynp", "modules", "provision"))
	if err != nil {
		t.Fatalf("resolve modules path: %v", err)
	}
	return path
}

func renderPrecheck(t *testing.T, isPreflightCheck bool) string {
	t.Helper()
	module := NewConfigureFips(configureFipsModulesPath(t))
	values := map[string]any{
		"os_family":          "RedHat",
		"os_distribution":    "almalinux",
		"is_airgap":          "False",
		"is_preflight_check": isPreflightCheck,
	}
	rendered, err := module.RenderTemplates(context.Background(), values)
	if err != nil {
		t.Fatalf("render templates: %v", err)
	}
	script := rendered.RenderedContent("precheck")
	scriptPath := filepath.Join(t.TempDir(), "precheck.sh")
	if err := os.WriteFile(scriptPath, []byte(script), 0o700); err != nil {
		t.Fatalf("write rendered script: %v", err)
	}
	if output, err := exec.Command("bash", "-n", scriptPath).CombinedOutput(); err != nil {
		t.Fatalf("rendered script has invalid shell syntax: %v\n%s", err, output)
	}
	return script
}

// The precheck that follows provisioning in the same pass runs before the reboot it asked for.
func TestPrecheckAfterProvisioningPassesOnPendingReboot(t *testing.T) {
	script := renderPrecheck(t, false)
	if !strings.Contains(script, `"PASS" "FIPS mode is configured and takes effect on reboot"`) {
		t.Fatalf("precheck does not pass a pending reboot:\n%s", script)
	}
	if strings.Contains(script, pendingRebootFailure) {
		t.Fatalf("precheck fails a pending reboot:\n%s", script)
	}
}

// The standalone preflight runs when the node is about to join a universe, where a pending
// reboot means FIPS is still off.
func TestPreflightFailsOnPendingReboot(t *testing.T) {
	script := renderPrecheck(t, true)
	if !strings.Contains(script, pendingRebootFailure) {
		t.Fatalf("preflight does not fail a pending reboot:\n%s", script)
	}
	if strings.Contains(script, `"PASS" "FIPS mode is configured and takes effect on reboot"`) {
		t.Fatalf("preflight passes a pending reboot:\n%s", script)
	}
}
