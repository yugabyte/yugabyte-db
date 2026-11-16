// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"os"
	"os/exec"
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

// Only a failed root stop may blame the sudoers rules; a failed state check never ran a stop.
func TestOtelCollectorStillRunningError(t *testing.T) {
	binary := "/home/yugabyte/otel-collector/otelcol-contrib"
	tests := []struct {
		rootStop  rootOtelCollectorStop
		wantCause string
	}{
		{rootOtelCollectorStopFailed, "sudoers"},
		{rootOtelCollectorStateUnknown, "could not be read"},
		{rootOtelCollectorNotInUse, "user systemd manager"},
	}
	for _, tt := range tests {
		msg := otelCollectorStillRunningError(binary, tt.rootStop).Error()
		if !strings.Contains(msg, binary) || !strings.Contains(msg, tt.wantCause) {
			t.Errorf("rootStop=%v: error %q must name %s and %q",
				tt.rootStop, msg, binary, tt.wantCause)
		}
		if tt.rootStop != rootOtelCollectorStopFailed && strings.Contains(msg, "sudoers") {
			t.Errorf("rootStop=%v: error %q must not blame the sudoers rules", tt.rootStop, msg)
		}
	}
}

// The directory step must survive a yb_home path with a space or a quote.
func TestEnsureOtelCollectorDirStepQuotesPath(t *testing.T) {
	ybHome := filepath.Join(t.TempDir(), "o'neil home")
	out, err := exec.Command("bash", "-c", ensureOtelCollectorDirStep(ybHome).Cmd).CombinedOutput()
	if err != nil {
		t.Fatalf("dir step: %v: %s", err, out)
	}
	info, err := os.Stat(filepath.Join(ybHome, "otel-collector"))
	if err != nil || !info.IsDir() || info.Mode().Perm() != 0o755 {
		t.Fatalf("want a 0755 directory, got info=%v err=%v", info, err)
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

// A slow poll misses a collector that runs only between checks. The 800 ms window leaves slack
// for a slow CI host, and a 2 s interval still fails: its only poll in 1500 ms is at 0 ms.
func TestOtelCollectorRunningPollIntervalFindsShortLivedCollector(t *testing.T) {
	start := time.Now()
	running, err := otelCollectorRunsWithin(
		context.Background(),
		1500*time.Millisecond,
		otelCollectorRunningPollInterval,
		func() (bool, error) {
			elapsed := time.Since(start)
			return elapsed > 600*time.Millisecond && elapsed < 1400*time.Millisecond, nil
		},
	)
	if err != nil || !running {
		t.Fatalf("collector alive for 800 ms not found with poll interval %v: running=%v, err=%v",
			otelCollectorRunningPollInterval, running, err)
	}
}

// fakePgrepEchoPattern makes the fake pgrep print the pattern it got, in place of a PID.
const fakePgrepEchoPattern = "pattern"

// runWithFakePgrep runs the process check with a pgrep that prints out and exits with rc.
func runWithFakePgrep(t *testing.T, binary, out string, rc int) (string, error) {
	dir := t.TempDir()
	script := `#!/bin/sh
if [ "$FAKE_PGREP_OUT" = "` + fakePgrepEchoPattern + `" ]; then printf '%s' "$2"
else printf '%s' "$FAKE_PGREP_OUT"; fi
exit "$FAKE_PGREP_RC"
`
	if err := os.WriteFile(filepath.Join(dir, "pgrep"), []byte(script), 0o755); err != nil {
		t.Fatalf("write fake pgrep: %v", err)
	}
	cmd := exec.Command("bash", "-c", otelCollectorProcessCheckCmd(binary))
	cmd.Env = append(os.Environ(),
		"PATH="+dir+":"+os.Getenv("PATH"),
		"FAKE_PGREP_OUT="+out,
		fmt.Sprintf("FAKE_PGREP_RC=%d", rc),
	)
	stdout, err := cmd.Output()
	return string(stdout), err
}

// Only pgrep exit 1 means "not running"; a pgrep error must fail the check.
func TestOtelCollectorProcessCheckCmdExitCodes(t *testing.T) {
	binary := "/home/yugabyte/otel-collector/otelcol-contrib"
	if out, err := runWithFakePgrep(t, binary, "123", 0); err != nil || out != "123" {
		t.Fatalf("exit 0: want PID and no error, got out=%q err=%v", out, err)
	}
	if out, err := runWithFakePgrep(t, binary, "", 1); err != nil || out != "" {
		t.Fatalf("exit 1: want no output and no error, got out=%q err=%v", out, err)
	}
	for _, rc := range []int{2, 3} {
		_, err := runWithFakePgrep(t, binary, "", rc)
		var exitErr *exec.ExitError
		if !errors.As(err, &exitErr) || exitErr.ExitCode() != rc {
			t.Fatalf("exit %d: want the same exit code as an error, got %v", rc, err)
		}
	}
}

// A path with a quote or a regex metacharacter must reach pgrep as a literal match.
func TestOtelCollectorProcessCheckCmdEscapesPath(t *testing.T) {
	binary := "/home/o'neil/yb.home/otel-collector/otelcol-contrib"
	out, err := runWithFakePgrep(t, binary, fakePgrepEchoPattern, 0)
	want := `^/home/o'neil/yb\.home/otel-collector/otelcol-contrib( |$)`
	if err != nil || out != want {
		t.Fatalf("want pattern %q, got %q (err %v)", want, out, err)
	}
	if _, err := exec.LookPath("pgrep"); err != nil {
		t.Skip("no pgrep on this host")
	}
	// The real pgrep finds a process that runs the binary, and not a sibling that differs only
	// where the unescaped "." would match.
	proc := exec.Command("bash", "-c", fmt.Sprintf(`exec -a %s sleep 30`,
		shellSingleQuote(binary+" --config=x")))
	decoy := exec.Command("bash", "-c", fmt.Sprintf(`exec -a %s sleep 30`,
		shellSingleQuote(strings.Replace(binary, "yb.home", "ybXhome", 1))))
	for _, c := range []*exec.Cmd{proc, decoy} {
		if err := c.Start(); err != nil {
			t.Fatalf("start process: %v", err)
		}
		t.Cleanup(func() { c.Process.Kill(); c.Wait() })
	}
	// bash needs a moment to exec into sleep, so poll until the deadline.
	wantPID := fmt.Sprint(proc.Process.Pid)
	var got string
	for deadline := time.Now().Add(5 * time.Second); time.Now().Before(deadline); {
		stdout, err := exec.Command("bash", "-c", otelCollectorProcessCheckCmd(binary)).Output()
		if err != nil {
			t.Fatalf("process check: %v", err)
		}
		if got = strings.TrimSpace(string(stdout)); got == wantPID {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}
	t.Fatalf("want only PID %s, got %q", wantPID, got)
}

// pgrep matches the command line text, so the path must not be cleaned.
func TestOtelCollectorUnitBinaryKeepsExecStartText(t *testing.T) {
	got := otelCollectorUnitBinary("/home/yugabyte/")
	want := "/home/yugabyte//otel-collector/" + otelCollectorInstalledBinary
	if got != want {
		t.Fatalf("want %q, got %q", want, got)
	}
}
