// Copyright (c) YugabyteDB, Inc.

package configureos

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// Must match memlock_bytes in run.j2, the DefaultLimitMEMLOCK it writes (50M).
const requiredMemlockBytes = "52428800"

func modulesPath(t *testing.T) string {
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

// render renders the module's templates with the values config.j2 gives ConfigureOs, and checks
// that both scripts are valid shell.
func render(t *testing.T, systemLevel, isPreflightCheck bool) (run, precheck string) {
	t.Helper()
	useSystemLevel := "False"
	if systemLevel {
		useSystemLevel = "True"
	}
	values := map[string]any{
		"use_system_level_systemd": useSystemLevel,
		"is_preflight_check":       isPreflightCheck,
		"is_cloud":                 "False",
		"is_yb_prebuilt_image":     "False",
		"os_family":                "RedHat",
		"os_distribution":          "almalinux",
		"fd_limit":                 "1048576",
		"nproc_limit":              "64000",
		"vm_swappiness":            "0",
		"vm_max_map_count":         "262144",
		"kernel_core_pattern":      "/home/yugabyte/cores/core_%p_%t_%E",
		"yb_home_dir":              "/home/yugabyte",
		"yb_user_home":             "/home/yugabyte",
		"yb_user":                  "yugabyte",
		"limits": map[string]any{
			"memlock": "51200",
			"nofile":  "1048576",
		},
	}
	rendered, err := NewConfigureOs(modulesPath(t)).RenderTemplates(context.Background(), values)
	if err != nil {
		t.Fatalf("render templates: %v", err)
	}
	run = rendered.RenderedContent("run")
	precheck = rendered.RenderedContent("precheck")
	for name, script := range map[string]string{"run": run, "precheck": precheck} {
		path := filepath.Join(t.TempDir(), name+".sh")
		if err := os.WriteFile(path, []byte(script), 0o700); err != nil {
			t.Fatalf("write %s: %v", path, err)
		}
		if out, err := exec.Command("bash", "-n", path).CombinedOutput(); err != nil {
			t.Fatalf("rendered %s script has invalid shell syntax: %v\n%s", name, err, out)
		}
	}
	return run, precheck
}

// shellFunc returns the definition of a top-level shell function in a rendered script, from its
// "name() {" line to the closing "}" at the start of a line. The run script changes /etc, so the
// tests only run the functions they cover.
func shellFunc(t *testing.T, script, name string) string {
	t.Helper()
	start := strings.Index(script, "\n"+name+"() {\n")
	if start < 0 {
		t.Fatalf("function %s not found in rendered script", name)
	}
	start++
	end := strings.Index(script[start:], "\n}\n")
	if end < 0 {
		t.Fatalf("end of function %s not found in rendered script", name)
	}
	return script[start : start+end+3]
}

func shellLine(t *testing.T, script, pattern string) string {
	t.Helper()
	line := regexp.MustCompile(pattern).FindString(script)
	if line == "" {
		t.Fatalf("no line matching %q in rendered script", pattern)
	}
	return line + "\n"
}

func writeFile(t *testing.T, path, content string, mode os.FileMode) {
	t.Helper()
	if err := os.WriteFile(path, []byte(content), mode); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func readFile(t *testing.T, path string) string {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil && !os.IsNotExist(err) {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(data)
}

// The stand-in yugabyte user. It is not 1000 so that a developer's own user@1000.service, which
// go test may run under, never matches.
const ybUID = "4242"

// userManagerEnv sets up stubs for the commands restart_yb_user_manager_for_memlock runs, in a
// state directory:
//   - active: present when user@<uid>.service is active.
//   - memlock, memlock_after: the manager's hard memlock before and after a restart, in bytes or
//     -1; a missing memlock file means it cannot be read.
//   - start_fails: present to make every start fail.
//   - reexec_fails: present to make the daemon-reexec fail.
//   - cgroup: stands in for /proc/self/cgroup.
//   - calls: the systemctl and su commands run, one per line.
func userManagerEnv(t *testing.T) (stateDir string, env []string) {
	t.Helper()
	dir := t.TempDir()
	stateDir = filepath.Join(dir, "state")
	stubDir := filepath.Join(dir, "bin")
	for _, d := range []string{stateDir, stubDir} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			t.Fatalf("create %s: %v", d, err)
		}
	}
	writeFile(t, filepath.Join(stubDir, "id"), "#!/bin/bash\necho "+ybUID+"\n", 0o755)
	writeFile(t, filepath.Join(stubDir, "su"),
		"#!/bin/bash\necho \"su $*\" >> \"$STATE_DIR/calls\"\n"+
			"[[ \"$*\" != *daemon-reexec* || ! -f \"$STATE_DIR/reexec_fails\" ]]\n", 0o755)
	writeFile(t, filepath.Join(stubDir, "systemctl"), `#!/bin/bash
echo "systemctl $*" >> "$STATE_DIR/calls"
case "$1" in
  is-active) [[ -f "$STATE_DIR/active" ]] ;;
  stop) rm -f "$STATE_DIR/active" ;;
  start)
    [[ -f "$STATE_DIR/start_fails" ]] && exit 1
    touch "$STATE_DIR/active"
    cp "$STATE_DIR/memlock_after" "$STATE_DIR/memlock"
    ;;
  show) cat "$STATE_DIR/main_pid" 2>/dev/null || echo 0 ;;
esac
`, 0o755)
	env = append(os.Environ(),
		"STATE_DIR="+stateDir,
		"PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	return stateDir, env
}

// Applying the memlock is best effort; only a manager that was stopped and cannot be started
// again fails provisioning.
func TestRestartYbUserManagerForMemlock(t *testing.T) {
	const nodeAgentCgroup = "0::/user.slice/user-" + ybUID + ".slice/user@" + ybUID +
		".service/app.slice/yb-node-agent.service\n"
	const sshSessionCgroup = "0::/user.slice/user-" + ybUID + ".slice/session-3.scope\n"
	tests := []struct {
		name         string
		active       bool
		memlock      string // Empty: the limit cannot be read.
		memlockAfter string
		startFails   bool
		reexecFails  bool
		cgroup       string
		wantRestart  bool
		wantFail     bool
		wantReexec   bool
		wantOutput   string
	}{
		{
			name:    "manager not running",
			memlock: "65536",
			cgroup:  sshSessionCgroup,
		},
		{
			name:       "limit cannot be read",
			active:     true,
			cgroup:     sshSessionCgroup,
			wantOutput: "WARNING: cannot read the memlock limit",
		},
		{
			name:       "limit already set",
			active:     true,
			memlock:    requiredMemlockBytes,
			cgroup:     sshSessionCgroup,
			wantReexec: true,
			wantOutput: "no restart needed",
		},
		{
			name:        "limit already set and daemon-reexec fails",
			active:      true,
			memlock:     requiredMemlockBytes,
			reexecFails: true,
			cgroup:      sshSessionCgroup,
			wantReexec:  true,
			wantOutput:  "WARNING: daemon-reexec of user@" + ybUID + ".service failed",
		},
		{
			name:       "limit unlimited",
			active:     true,
			memlock:    "-1",
			cgroup:     sshSessionCgroup,
			wantReexec: true,
			wantOutput: "no restart needed",
		},
		{
			name:         "limit too low",
			active:       true,
			memlock:      "65536",
			memlockAfter: requiredMemlockBytes,
			cgroup:       sshSessionCgroup,
			wantRestart:  true,
		},
		{
			name:       "limit too low and run under the user manager",
			active:     true,
			memlock:    "65536",
			cgroup:     nodeAgentCgroup,
			wantOutput: "WARNING: provisioning runs under",
		},
		{
			name:         "restart does not raise the limit",
			active:       true,
			memlock:      "65536",
			memlockAfter: "65536",
			cgroup:       sshSessionCgroup,
			wantRestart:  true,
			wantOutput:   "WARNING: user@" + ybUID + ".service memlock is still 65536 after restart",
		},
		{
			name:        "manager does not start again",
			active:      true,
			memlock:     "65536",
			startFails:  true,
			cgroup:      sshSessionCgroup,
			wantRestart: true,
			wantFail:    true,
			wantOutput:  "Failed to start user@" + ybUID + ".service after stopping it",
		},
	}
	run, _ := render(t, false /*systemLevel*/, false /*isPreflightCheck*/)
	harness := shellLine(t, run, `(?m)^memlock_bytes=.*$`) +
		shellFunc(t, run, "yb_user_manager_memlock") +
		shellFunc(t, run, "restart_yb_user_manager_for_memlock") + `
# Read the limit from the state directory; TestYbUserManagerMemlock covers the real one.
yb_user_manager_memlock() {
  [[ -f "$STATE_DIR/memlock" ]] || return 1
  cat "$STATE_DIR/memlock"
}
# Read the stand-in for /proc/self/cgroup.
grep() {
  local args=() arg
  for arg in "$@"; do
    [[ "$arg" == /proc/self/cgroup ]] && arg="$STATE_DIR/cgroup"
    args+=("$arg")
  done
  command grep "${args[@]}"
}
sleep() { :; }
restart_yb_user_manager_for_memlock
`
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			stateDir, env := userManagerEnv(t)
			writeFile(t, filepath.Join(stateDir, "cgroup"), tc.cgroup, 0o644)
			if tc.active {
				writeFile(t, filepath.Join(stateDir, "active"), "", 0o644)
			}
			if tc.memlock != "" {
				writeFile(t, filepath.Join(stateDir, "memlock"), tc.memlock+"\n", 0o644)
			}
			if tc.memlockAfter != "" {
				writeFile(t, filepath.Join(stateDir, "memlock_after"), tc.memlockAfter+"\n", 0o644)
			}
			if tc.startFails {
				writeFile(t, filepath.Join(stateDir, "start_fails"), "", 0o644)
			}
			if tc.reexecFails {
				writeFile(t, filepath.Join(stateDir, "reexec_fails"), "", 0o644)
			}
			cmd := exec.Command("bash", "-c", harness)
			cmd.Env = env
			out, err := cmd.CombinedOutput()
			if failed := err != nil; failed != tc.wantFail {
				t.Fatalf("failed = %t (%v), want %t:\n%s", failed, err, tc.wantFail, out)
			}
			if !strings.Contains(string(out), tc.wantOutput) {
				t.Errorf("output does not contain %q:\n%s", tc.wantOutput, out)
			}
			calls := readFile(t, filepath.Join(stateDir, "calls"))
			restart := "systemctl stop user@" + ybUID + ".service\n" +
				"systemctl reset-failed user@" + ybUID + ".service\n" +
				"systemctl start user@" + ybUID + ".service\n"
			if got := strings.Contains(calls, restart); got != tc.wantRestart {
				t.Errorf("restarted = %t, want %t; calls:\n%s", got, tc.wantRestart, calls)
			}
			if got := strings.Contains(calls, "systemctl --user daemon-reexec"); got != tc.wantReexec {
				t.Errorf("re-executed = %t, want %t; calls:\n%s", got, tc.wantReexec, calls)
			}
		})
	}
}

// yb_user_manager_memlock reads the hard limit from /proc/<MainPID>/limits in bytes.
func TestYbUserManagerMemlock(t *testing.T) {
	run, _ := render(t, false /*systemLevel*/, false /*isPreflightCheck*/)
	// The harness lowers its own memlock to 64 KiB and stands in for the manager.
	harness := shellFunc(t, run, "yb_user_manager_memlock") + `
ulimit -l 64 || exit 2
echo $$ > "$STATE_DIR/main_pid"
yb_user_manager_memlock ` + ybUID + ` || echo "no limit"
rm "$STATE_DIR/main_pid"
yb_user_manager_memlock ` + ybUID + ` || echo "no limit"
`
	_, env := userManagerEnv(t)
	cmd := exec.Command("bash", "-c", harness)
	cmd.Env = env
	out, err := cmd.CombinedOutput()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok && exitErr.ExitCode() == 2 {
			t.Skipf("cannot lower memlock to 64 KiB here:\n%s", out)
		}
		t.Fatalf("harness failed: %v\n%s", err, out)
	}
	if got, want := string(out), "65536\nno limit\n"; got != want {
		t.Fatalf("output = %q, want %q", got, want)
	}
}

// On el8+ user-level provisioning stops and starts the user manager, which then gets the new
// limits, so restart_yb_user_manager_for_memlock is needed only in the other branch. System level
// does not exit the manager on every run, but can still restart it to raise memlock.
func TestRunAppliesMemlockToUserManager(t *testing.T) {
	for _, systemLevel := range []bool{false, true} {
		run, _ := render(t, systemLevel, false /*isPreflightCheck*/)
		if got := strings.Contains(run, "systemctl --user exit"); got == systemLevel {
			t.Errorf("system level %t: run script stops the user manager = %t", systemLevel, got)
		}
		calls := strings.Count(run, "\nrestart_yb_user_manager_for_memlock\n") +
			strings.Count(run, "\n    restart_yb_user_manager_for_memlock\n")
		if calls != 1 {
			t.Errorf("system level %t: restart_yb_user_manager_for_memlock called %d times, want 1",
				systemLevel, calls)
		}
	}
}

// Only the standalone preflight checks ulimits, and only memlock has a FAIL floor.
func TestPrecheckUlimitCalls(t *testing.T) {
	_, precheck := render(t, false /*systemLevel*/, true /*isPreflightCheck*/)
	for _, want := range []string{
		"\ncheck_ulimit \"memlock\" \"51200\" 64\n",
		"\ncheck_ulimit \"nofile\" \"1048576\"\n",
	} {
		if !strings.Contains(precheck, want) {
			t.Errorf("preflight does not contain %q:\n%s", want, precheck)
		}
	}
	_, precheck = render(t, false /*systemLevel*/, false /*isPreflightCheck*/)
	if strings.Contains(precheck, "\ncheck_ulimit ") {
		t.Errorf("precheck after provisioning checks ulimits:\n%s", precheck)
	}
}

// Nodes provisioned with "* - memlock 64" keep that limit until they are re-provisioned, and the
// preflight sees it through the node agent. It must warn there, not fail.
func TestCheckUlimitMemlock(t *testing.T) {
	tests := []struct {
		name      string
		current   string
		failBelow string
		want      string
	}{
		{name: "new limit", current: "51200", failBelow: "64", want: "PASS"},
		{name: "unlimited", current: "unlimited", failBelow: "64", want: "PASS"},
		{name: "node provisioned before the change", current: "64", failBelow: "64", want: "WARN"},
		{name: "below the previous requirement", current: "32", failBelow: "64", want: "FAIL"},
		{name: "no floor", current: "64", want: "FAIL"},
	}
	_, precheck := render(t, false /*systemLevel*/, true /*isPreflightCheck*/)
	checkUlimit := shellFunc(t, precheck, "check_ulimit")
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			harness := checkUlimit + `
add_result() { echo "RESULT $1 $2"; }
ulimit() {
  if [[ "$1" == -l ]]; then echo "$CURRENT"; else builtin ulimit "$@"; fi
}
check_ulimit memlock 51200 ` + tc.failBelow + `
`
			cmd := exec.Command("bash", "-c", harness)
			cmd.Env = append(os.Environ(), "CURRENT="+tc.current)
			out, err := cmd.CombinedOutput()
			if err != nil {
				t.Fatalf("harness failed: %v\n%s", err, out)
			}
			if want := "RESULT memlock " + tc.want + "\n"; !strings.Contains(string(out), want) {
				t.Fatalf("output does not contain %q:\n%s", want, out)
			}
		})
	}
}
