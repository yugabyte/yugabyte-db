// Copyright (c) YugabyteDB, Inc.

package systemd

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func projectDir(t *testing.T) string {
	t.Helper()
	if dir := os.Getenv("PROJECT_DIR"); dir != "" {
		return dir
	}
	dir, err := filepath.Abs(filepath.Join("..", "..", "..", ".."))
	if err != nil {
		t.Fatalf("resolve project dir: %v", err)
	}
	return dir
}

// stageModule lays the module out the way build.sh ships it: the module's own templates with the
// server templates copied in next to them.
func stageModule(t *testing.T) string {
	t.Helper()
	base := t.TempDir()
	dest := filepath.Join(base, "systemd", "templates")
	if err := os.MkdirAll(dest, 0o755); err != nil {
		t.Fatalf("create %s: %v", dest, err)
	}
	project := projectDir(t)
	for _, src := range []string{
		filepath.Join(project, "resources", "ynp", "modules", "provision", "systemd", "templates"),
		filepath.Join(project, "resources", "templates", "server"),
	} {
		entries, err := os.ReadDir(src)
		if err != nil {
			t.Fatalf("read %s: %v", src, err)
		}
		for _, entry := range entries {
			if entry.IsDir() {
				continue
			}
			data, err := os.ReadFile(filepath.Join(src, entry.Name()))
			if err != nil {
				t.Fatalf("read %s: %v", entry.Name(), err)
			}
			if err := os.WriteFile(filepath.Join(dest, entry.Name()), data, 0o644); err != nil {
				t.Fatalf("write %s: %v", entry.Name(), err)
			}
		}
	}
	return base
}

// newYbHome creates a yb user home, with bin/ when withBin is set, as on a node that already ran
// the servers.
func newYbHome(t *testing.T, withBin bool) string {
	t.Helper()
	ybHome := filepath.Join(t.TempDir(), "yugabyte")
	dir := ybHome
	if withBin {
		dir = filepath.Join(ybHome, "bin")
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatalf("create %s: %v", dir, err)
	}
	return ybHome
}

// runScript renders the module's run script for a yb user home at ybHome and runs it as that user
// (no sudo), returning its combined output.
func runScript(t *testing.T, ybHome string, configureCgroup bool) ([]byte, error) {
	t.Helper()
	return runScriptIn(t, t.TempDir(), ybHome, configureCgroup)
}

// runScriptIn is runScript with scratch as the script's working files and TMPDIR.
func runScriptIn(
	t *testing.T, scratch, ybHome string, configureCgroup bool) ([]byte, error) {
	t.Helper()
	cgroup := "False"
	if configureCgroup {
		cgroup = "True"
	}
	values := map[string]any{
		"use_system_level_systemd": "False",
		"is_install_node_agent":    "False",
		"configure_cgroup":         cgroup,
		"service_files":            "yb-master.service, yb-tserver.service",
		"yb_home_dir":              ybHome,
		"yb_user_home":             ybHome,
		"yb_user":                  "yugabyte",
	}
	rendered, err := NewConfigureSystemd(stageModule(t)).RenderTemplates(
		context.Background(), values)
	if err != nil {
		t.Fatalf("render templates: %v", err)
	}
	scriptPath := filepath.Join(scratch, "run.sh")
	if err := os.WriteFile(
		scriptPath, []byte(rendered.RenderedContent("run")), 0o700); err != nil {
		t.Fatalf("write rendered script: %v", err)
	}
	// Stubs keep the script off the host's systemd: no root units, and a no-op daemon-reload.
	stubDir := filepath.Join(scratch, "bin")
	if err := os.MkdirAll(stubDir, 0o755); err != nil {
		t.Fatalf("create %s: %v", stubDir, err)
	}
	if err := os.WriteFile(
		filepath.Join(stubDir, "systemctl"), []byte("#!/bin/sh\nexit 1\n"), 0o755); err != nil {
		t.Fatalf("write systemctl stub: %v", err)
	}
	cmd := exec.Command("bash", scriptPath)
	cmd.Env = append(os.Environ(),
		"SUDO_ACCESS=false",
		"TMPDIR="+scratch,
		"PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	return cmd.CombinedOutput()
}

func readUnit(t *testing.T, ybHome, unit string) string {
	t.Helper()
	data, err := os.ReadFile(filepath.Join(ybHome, ".config", "systemd", "user", unit))
	if err != nil {
		t.Fatalf("read %s: %v", unit, err)
	}
	return string(data)
}

func assertInstalledLauncher(t *testing.T, ybHome, name string) {
	t.Helper()
	launcher := filepath.Join(ybHome, "bin", name)
	info, err := os.Stat(launcher)
	if err != nil {
		t.Fatalf("launcher not installed: %v", err)
	}
	if info.Mode().Perm()&0o111 == 0 {
		t.Fatalf("%s is not executable: %v", name, info.Mode())
	}
	got, err := os.ReadFile(launcher)
	if err != nil {
		t.Fatalf("read %s: %v", name, err)
	}
	want, err := os.ReadFile(
		filepath.Join(projectDir(t), "resources", "templates", "server", name+".j2"))
	if err != nil {
		t.Fatalf("read %s template: %v", name, err)
	}
	if strings.TrimRight(string(got), "\n") != strings.TrimRight(string(want), "\n") {
		t.Fatalf("installed %s differs from the template:\n%s", name, got)
	}
}

// Re-provisioning a live node writes the units and then starts the servers without running
// ConfigureServer, so the launchers the units exec have to come with them (PLAT-23460).
func TestRunInstallsLaunchersWithUnits(t *testing.T) {
	ybHome := newYbHome(t, true /* withBin */)
	if output, err := runScript(t, ybHome, true /* configureCgroup */); err != nil {
		t.Fatalf("run script: %v\n%s", err, output)
	}
	serverExec := filepath.Join(ybHome, "bin", "yb-server-exec.sh")
	cgroupExec := filepath.Join(ybHome, "bin", "yb-tserver-cgroup-exec.sh")
	if master := readUnit(t, ybHome, "yb-master.service"); !strings.Contains(
		master, "ExecStart="+serverExec+" ") {
		t.Fatalf("yb-master.service does not exec through %s:\n%s", serverExec, master)
	}
	tserver := readUnit(t, ybHome, "yb-tserver.service")
	if !strings.Contains(tserver, "ExecStart="+serverExec+" ") ||
		!strings.Contains(tserver, cgroupExec) {
		t.Fatalf("yb-tserver.service does not exec through both launchers:\n%s", tserver)
	}
	assertInstalledLauncher(t, ybHome, "yb-server-exec.sh")
	assertInstalledLauncher(t, ybHome, "yb-tserver-cgroup-exec.sh")
	if output, err := exec.Command(serverExec, "true").CombinedOutput(); err != nil {
		t.Fatalf("yb-server-exec.sh does not exec its command: %v\n%s", err, output)
	}
}

// The tserver unit only execs the cgroup launcher when cgroups are configured.
func TestRunSkipsCgroupLauncherWithoutCgroup(t *testing.T) {
	ybHome := newYbHome(t, true /* withBin */)
	if output, err := runScript(t, ybHome, false /* configureCgroup */); err != nil {
		t.Fatalf("run script: %v\n%s", err, output)
	}
	assertInstalledLauncher(t, ybHome, "yb-server-exec.sh")
	if _, err := os.Stat(filepath.Join(ybHome, "bin", "yb-tserver-cgroup-exec.sh")); err == nil {
		t.Fatalf("cgroup launcher installed without cgroups")
	}
}

// A blank node gets the launchers from ConfigureServer. Creating bin/ there would fail the clean
// home directory check in the yugabyte precheck for manual on-prem nodes.
func TestRunLeavesBlankNodeWithoutBin(t *testing.T) {
	ybHome := newYbHome(t, false /* withBin */)
	if output, err := runScript(t, ybHome, true /* configureCgroup */); err != nil {
		t.Fatalf("run script: %v\n%s", err, output)
	}
	readUnit(t, ybHome, "yb-master.service")
	if _, err := os.Stat(filepath.Join(ybHome, "bin")); err == nil {
		t.Fatalf("bin/ created on a blank node")
	}
}

// The module scripts do not run with set -e, so a failed launcher install has to fail the module
// itself rather than leave the units pointing at a missing file.
func TestRunFailsWhenLauncherInstallFails(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("root can write to a read-only bin/")
	}
	ybHome := newYbHome(t, true /* withBin */)
	bin := filepath.Join(ybHome, "bin")
	if err := os.Chmod(bin, 0o555); err != nil {
		t.Fatalf("chmod %s: %v", bin, err)
	}
	t.Cleanup(func() { os.Chmod(bin, 0o755) })
	scratch := t.TempDir()
	output, err := runScriptIn(t, scratch, ybHome, false /* configureCgroup */)
	if err == nil {
		t.Fatalf("script succeeded although the launcher could not be installed:\n%s", output)
	}
	if !strings.Contains(string(output), "Failed to install "+bin+"/yb-server-exec.sh") {
		t.Fatalf("script did not report the failed install:\n%s", output)
	}
	leftover, err := filepath.Glob(filepath.Join(scratch, "tmp.*"))
	if err != nil {
		t.Fatalf("glob temp files: %v", err)
	}
	if len(leftover) > 0 {
		t.Fatalf("failed install left temp files behind: %v", leftover)
	}
}
