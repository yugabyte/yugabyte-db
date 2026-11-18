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

// skipIfUserOtelCollectorUnit skips a test that runs the disable on this host.
func skipIfUserOtelCollectorUnit(t *testing.T, handler *InstallOtelCollector) {
	hasUser, _, err := module.IsUserSystemd(handler.username, OtelCollectorService)
	if err != nil {
		t.Fatalf("IsUserSystemd: %v", err)
	}
	if hasUser {
		t.Skip("this host has a user-level otel-collector unit")
	}
}

// The disable path runs without the package setup, on nodes that may never have had the
// collector: no unit to stop and no <yb_home>/otel-collector directory. A yb_home with a space
// or a quote must stay one shell argument.
func TestDisableOtelCollectorWithoutUnitOrDir(t *testing.T) {
	for _, name := range []string{"yugabyte", "yb home", "o'neil yb"} {
		parent := t.TempDir()
		ybHome := filepath.Join(parent, name)
		// An unquoted "yb home" path splits, and "rm -rf <parent>/yb" removes this directory.
		decoy := filepath.Join(parent, "yb")
		if err := os.MkdirAll(decoy, 0755); err != nil {
			t.Fatalf("mkdir decoy: %v", err)
		}
		handler := newDisableCollectorHandler(t, ybHome)
		handler.stopRootUnit = func(context.Context) rootOtelCollectorStop {
			return rootOtelCollectorNotRunning
		}
		skipIfUserOtelCollectorUnit(t, handler)
		// The awk and mv steps that strip the managed block run only on an existing file.
		creds := filepath.Join(ybHome, ".aws", "credentials")
		if err := os.MkdirAll(filepath.Dir(creds), 0755); err != nil {
			t.Fatalf("mkdir: %v", err)
		}
		block := "# BEGIN YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS\n[otel-collector]\n" +
			"# END YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS\n"
		if err := os.WriteFile(creds, []byte("[default]\n"+block), 0600); err != nil {
			t.Fatalf("write credentials: %v", err)
		}

		if err := handler.disableOtelCollector(context.Background(), ybHome); err != nil {
			t.Fatalf("%q: disableOtelCollector: %v", name, err)
		}

		otelDir := filepath.Join(ybHome, "otel-collector")
		if _, err := os.Stat(filepath.Join(otelDir, "log_cleanup_env")); err != nil {
			t.Fatalf("%q: log_cleanup_env was not written: %v", name, err)
		}
		if _, err := os.Stat(filepath.Join(otelDir, "config.yml")); !os.IsNotExist(err) {
			t.Fatalf("%q: config.yml should not exist, got err=%v", name, err)
		}
		if _, err := os.Stat(decoy); err != nil {
			t.Fatalf("%q: the disable touched a path outside yb_home: %v", name, err)
		}
		if got, err := os.ReadFile(creds); err != nil || string(got) != "[default]\n" {
			t.Fatalf("%q: want only the managed block removed, got %q, err=%v", name, got, err)
		}
	}
}

// A root stop failure never fails the disable, as gflags upgrades and node operations run it
// too. The disable still removes the config and refreshes the purge script, and a root
// collector that may still run gets one warning in the task output.
func TestHandleDisableWithRootStopFailure(t *testing.T) {
	for _, rootStop := range []rootOtelCollectorStop{
		rootOtelCollectorStopFailed,
		rootOtelCollectorStateUnknown,
	} {
		ybHome := t.TempDir()
		handler := newDisableCollectorHandler(t, ybHome)
		handler.stopRootUnit = func(context.Context) rootOtelCollectorStop { return rootStop }
		refreshed := false
		handler.refreshPurge = func(context.Context, string) error {
			refreshed = true
			return nil
		}
		skipIfUserOtelCollectorUnit(t, handler)
		config := filepath.Join(ybHome, "otel-collector", "config.yml")
		if err := os.MkdirAll(filepath.Dir(config), 0755); err != nil {
			t.Fatalf("mkdir: %v", err)
		}
		if err := os.WriteFile(config, []byte("receivers:\n"), 0644); err != nil {
			t.Fatalf("write config: %v", err)
		}

		if _, err := handler.Handle(context.Background()); err != nil {
			t.Fatalf("rootStop=%v: want a warning only, got %v", rootStop, err)
		}
		if !refreshed {
			t.Fatalf("rootStop=%v: the purge script refresh did not run", rootStop)
		}
		if _, err := os.Stat(config); !os.IsNotExist(err) {
			t.Fatalf("rootStop=%v: config.yml should be removed, got err=%v", rootStop, err)
		}
		want := map[rootOtelCollectorStop]string{
			rootOtelCollectorStopFailed:   "Check its sudoers rules",
			rootOtelCollectorStateUnknown: "could not be read",
		}[rootStop]
		out := handler.logOut.String()
		if strings.Count(out, "keep running and exporting") != 1 ||
			!strings.Contains(out, want) {
			t.Fatalf("rootStop=%v: want one export warning with %q, got %q", rootStop, want, out)
		}
	}
}

// The install stops the root collector last, after the user stop and the configure pass, so a
// failed step leaves it exporting. Only a failed user stop names it. A failed root stop warns.
func TestStopOtelCollectorsForInstall(t *testing.T) {
	userErr := errors.New("user stop failed")
	configErr := errors.New("configure failed")
	tests := []struct {
		name      string
		rootInUse bool
		rootErr   error
		userErr   error
		configErr error
		rootStop  rootOtelCollectorStop
		wantCalls string
		wantErr   error
		wantRoot  bool
		want      string
	}{
		{name: "user stop fails", userErr: userErr, wantCalls: "user", wantErr: userErr},
		{name: "user stop fails, root in use", rootInUse: true, userErr: userErr, wantCalls: "user",
			wantErr: userErr, wantRoot: true},
		{name: "user stop fails, root state unknown", rootErr: errors.New("is-active failed"),
			userErr: userErr, wantCalls: "user", wantErr: userErr, wantRoot: true},
		{name: "configure fails", configErr: configErr, wantCalls: "user,configure",
			wantErr: configErr},
		{name: "root stopped", rootStop: rootOtelCollectorNotRunning,
			wantCalls: "user,configure,root"},
		{name: "root stop fails", rootStop: rootOtelCollectorStopFailed,
			wantCalls: "user,configure,root", want: "Check its sudoers rules"},
		{name: "root state unknown", rootStop: rootOtelCollectorStateUnknown,
			wantCalls: "user,configure,root", want: "could not be read"},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			handler := newDisableCollectorHandler(t, t.TempDir())
			handler.rootInUse = func(context.Context) (bool, error) {
				return tc.rootInUse, tc.rootErr
			}
			var calls []string
			handler.disableUserUnit = func(context.Context) error {
				calls = append(calls, "user")
				return tc.userErr
			}
			handler.stopRootUnit = func(context.Context) rootOtelCollectorStop {
				calls = append(calls, "root")
				return tc.rootStop
			}
			err := handler.stopOtelCollectorsForInstall(context.Background(), func() error {
				calls = append(calls, "configure")
				return tc.configErr
			})
			if !errors.Is(err, tc.wantErr) {
				t.Fatalf("got err %v, want %v", err, tc.wantErr)
			}
			if err != nil && strings.Contains(err.Error(), "keep running") != tc.wantRoot {
				t.Fatalf("root collector named in the error=%v, want %v: %v",
					!tc.wantRoot, tc.wantRoot, err)
			}
			if got := strings.Join(calls, ","); got != tc.wantCalls {
				t.Fatalf("calls %q, want %q", got, tc.wantCalls)
			}
			out := handler.logOut.String()
			if tc.want == "" {
				if out != "" {
					t.Fatalf("want no task output, got %q", out)
				}
				return
			}
			if strings.Count(out, "keep running and exporting") != 1 ||
				!strings.Contains(out, tc.want) {
				t.Fatalf("want one export warning with %q, got %q", tc.want, out)
			}
		})
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

func TestRootOtelCollectorRunning(t *testing.T) {
	for _, active := range []string{
		"active", "activating", "reloading", "deactivating", "refreshing",
	} {
		if !rootOtelCollectorRunning(active) {
			t.Errorf("%q must count as running", active)
		}
	}
	for _, active := range []string{"inactive", "failed", "unknown", ""} {
		if rootOtelCollectorRunning(active) {
			t.Errorf("%q must not count as running", active)
		}
	}
}

// After a failed root stop, a root unit that still runs or starts at boot counts as failed.
func TestRootOtelCollectorStopOutcome(t *testing.T) {
	tests := []struct {
		active, enabled string
		checkErr        error
		want            rootOtelCollectorStop
	}{
		{"active", "enabled", nil, rootOtelCollectorStopFailed},
		{"inactive", "enabled", nil, rootOtelCollectorStopFailed},
		{"inactive", "disabled", nil, rootOtelCollectorNotRunning},
		{"failed", "disabled", nil, rootOtelCollectorNotRunning},
		{"", "", errors.New("no state"), rootOtelCollectorStateUnknown},
		{"active", "", errors.New("no is-enabled state"), rootOtelCollectorStopFailed},
	}
	for _, tt := range tests {
		got := rootOtelCollectorStopOutcome(tt.active, tt.enabled, tt.checkErr)
		if got != tt.want {
			t.Errorf("(%q, %q, %v) = %v, want %v", tt.active, tt.enabled, tt.checkErr, got, tt.want)
		}
	}
}

// The root stop must not wait for a password, and the YNP sudoers rules name the unit without
// the .service suffix.
func TestRootOtelCollectorStopSteps(t *testing.T) {
	var got []string
	for _, step := range rootOtelCollectorStopSteps() {
		got = append(got, step.Cmd)
	}
	want := []string{
		"sudo -n systemctl stop otel-collector",
		"sudo -n systemctl disable otel-collector",
	}
	if strings.Join(got, "\n") != strings.Join(want, "\n") {
		t.Fatalf("want %q, got %q", want, got)
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
	msg := otelCollectorStillRunningError(binary).Error()
	if !strings.Contains(msg, binary) || !strings.Contains(msg, "user systemd manager") {
		t.Fatalf("error %q must name %s and the user systemd manager", msg, binary)
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

// runWithFakePgrep runs the process check with a pgrep that prints out and exits with rc. In echo
// mode, the fake prints its pattern to stderr.
func runWithFakePgrep(
	t *testing.T,
	binary, procDir, out string,
	rc int,
) (string, string, error) {
	dir := t.TempDir()
	script := `#!/bin/sh
if [ "$FAKE_PGREP_OUT" = "` + fakePgrepEchoPattern + `" ]; then printf '%s' "$2" >&2
else printf '%s' "$FAKE_PGREP_OUT"; fi
exit "$FAKE_PGREP_RC"
`
	if err := os.WriteFile(filepath.Join(dir, "pgrep"), []byte(script), 0o755); err != nil {
		t.Fatalf("write fake pgrep: %v", err)
	}
	cmd := exec.Command("bash", "-c", otelCollectorProcessCheckCmd(binary, procDir))
	cmd.Env = append(os.Environ(),
		"PATH="+dir+":"+os.Getenv("PATH"),
		"FAKE_PGREP_OUT="+out,
		fmt.Sprintf("FAKE_PGREP_RC=%d", rc),
	)
	var stderr strings.Builder
	cmd.Stderr = &stderr
	stdout, err := cmd.Output()
	return strings.TrimSpace(string(stdout)), stderr.String(), err
}

// Only pgrep exit 1 means "not running"; a pgrep error must fail the check.
func TestOtelCollectorProcessCheckCmdExitCodes(t *testing.T) {
	binary := "/home/yugabyte/otel-collector/otelcol-contrib"
	procDir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(procDir, "123"), 0755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	userCgroup := []byte("0::/user.slice/" + OtelCollectorService + "\n")
	if err := os.WriteFile(filepath.Join(procDir, "123", "cgroup"), userCgroup, 0644); err != nil {
		t.Fatalf("write cgroup: %v", err)
	}
	if out, _, err := runWithFakePgrep(t, binary, procDir, "123", 0); err != nil || out != "123" {
		t.Fatalf("exit 0: want PID and no error, got out=%q err=%v", out, err)
	}
	if out, _, err := runWithFakePgrep(t, binary, procDir, "", 1); err != nil || out != "" {
		t.Fatalf("exit 1: want no output and no error, got out=%q err=%v", out, err)
	}
	for _, rc := range []int{2, 3} {
		_, _, err := runWithFakePgrep(t, binary, procDir, "", rc)
		var exitErr *exec.ExitError
		if !errors.As(err, &exitErr) || exitErr.ExitCode() != rc {
			t.Fatalf("exit %d: want the same exit code as an error, got %v", rc, err)
		}
	}
}

// The root unit's collector gets its own warning, so the check must skip it and keep the others.
// A PID that exited after pgrep is not running.
func TestOtelCollectorProcessCheckCmdSkipsRootUnit(t *testing.T) {
	procDir := t.TempDir()
	cgroups := map[string]string{
		"123": "0::" + otelCollectorRootCgroup + "\n",
		"456": "0::/user.slice/user-1000.slice/user@1000.service/app.slice/" +
			OtelCollectorService + "\n",
	}
	for pid, cgroup := range cgroups {
		if err := os.MkdirAll(filepath.Join(procDir, pid), 0755); err != nil {
			t.Fatalf("mkdir: %v", err)
		}
		err := os.WriteFile(filepath.Join(procDir, pid, "cgroup"), []byte(cgroup), 0644)
		if err != nil {
			t.Fatalf("write cgroup: %v", err)
		}
	}
	binary := "/home/yugabyte/otel-collector/otelcol-contrib"
	// PID 789 exited after pgrep, so it has no /proc entry.
	out, _, err := runWithFakePgrep(t, binary, procDir, "123\n456\n789", 0)
	if err != nil || out != "456" {
		t.Fatalf("want only the user collector PID 456, got out=%q err=%v", out, err)
	}
}

// A path with a quote or a regex metacharacter must reach pgrep as a literal match.
func TestOtelCollectorProcessCheckCmdEscapesPath(t *testing.T) {
	binary := "/home/o'neil/yb.home/otel-collector/otelcol-contrib"
	_, pattern, err := runWithFakePgrep(t, binary, t.TempDir(), fakePgrepEchoPattern, 0)
	want := `^/home/o'neil/yb\.home/otel-collector/otelcol-contrib( |$)`
	if err != nil || pattern != want {
		t.Fatalf("want pattern %q, got %q (err %v)", want, pattern, err)
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
		cmd := otelCollectorProcessCheckCmd(binary, "/proc")
		stdout, err := exec.Command("bash", "-c", cmd).Output()
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
