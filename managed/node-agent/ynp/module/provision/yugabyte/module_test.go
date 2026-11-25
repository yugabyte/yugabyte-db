// Copyright (c) YugabyteDB, Inc.

package yugabyte

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

const (
	homeNotClean = `"Yugabyte Home Directory Clean Check" "FAIL"`
	homeClean    = `"Yugabyte Home Directory Clean Check" "PASS"`
)

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

// runPrecheck renders the module's precheck for a yb home at ybHome, as YBA's preflight check on a
// manual on-prem blank node does, runs it, and returns the add_result calls it made.
func runPrecheck(t *testing.T, ybHome string) string {
	t.Helper()
	values := map[string]any{
		"yb_user":                      "yugabyte",
		"yb_user_home":                 ybHome,
		"yb_home_dir":                  ybHome,
		"check_clean_dirs":             "True",
		"mount_paths":                  "",
		"tmp_directory":                os.TempDir(),
		"min_home_dir_space_gb":        0,
		"min_user_home_space_gb":       0,
		"min_mount_point_dir_space_gb": 0,
		"min_tmp_dir_space_gb":         0,
	}
	rendered, err := NewCreateYugabyteUser(modulesPath(t)).RenderTemplates(
		context.Background(), values)
	if err != nil {
		t.Fatalf("render templates: %v", err)
	}
	// Stand-ins for the helpers provision_command.go wraps the precheck in.
	helpers := `add_result() { echo "\"$1\" \"$2\" \"$3\""; }
add_fatal_result() { echo "\"$1\" \"FATAL\" \"$2\""; }
`
	scriptPath := filepath.Join(t.TempDir(), "precheck.sh")
	script := helpers + rendered.RenderedContent("precheck")
	if err := os.WriteFile(scriptPath, []byte(script), 0o700); err != nil {
		t.Fatalf("write rendered script: %v", err)
	}
	output, err := exec.Command("bash", scriptPath).CombinedOutput()
	if err != nil {
		t.Fatalf("run precheck: %v\n%s", err, output)
	}
	return string(output)
}

func newYbHome(t *testing.T, entries ...string) string {
	t.Helper()
	ybHome := filepath.Join(t.TempDir(), "yugabyte")
	if err := os.MkdirAll(ybHome, 0o755); err != nil {
		t.Fatalf("create %s: %v", ybHome, err)
	}
	for _, entry := range entries {
		path := filepath.Join(ybHome, entry)
		var err error
		if strings.HasSuffix(entry, "/") {
			err = os.MkdirAll(path, 0o755)
		} else {
			if err = os.MkdirAll(filepath.Dir(path), 0o755); err == nil {
				err = os.WriteFile(path, []byte("#!/bin/sh\n"), 0o755)
			}
		}
		if err != nil {
			t.Fatalf("create %s: %v", path, err)
		}
	}
	return ybHome
}

// On SLES, useradd copies /etc/skel/bin into the new home, so a blank node has an empty bin/
// (PLAT-24135).
func TestHomeCleanCheckPassesWithEmptySkelBin(t *testing.T) {
	output := runPrecheck(t, newYbHome(t, "bin/", ".bashrc"))
	if !strings.Contains(output, homeClean) || strings.Contains(output, homeNotClean) {
		t.Fatalf("empty bin/ fails the clean home directory check:\n%s", output)
	}
}

// Leftover server files still make the home directory unclean.
func TestHomeCleanCheckFailsOnLeftovers(t *testing.T) {
	for _, entries := range [][]string{
		{"bin/yb-server-ctl.sh"},
		{"bin/yb-server-exec.sh"},
		{"master/"},
		{"tserver/"},
		{"controller/"},
		{".yugabytedb/"},
	} {
		output := runPrecheck(t, newYbHome(t, entries...))
		if !strings.Contains(output, homeNotClean) {
			t.Fatalf("home with %v passes the clean home directory check:\n%s", entries, output)
		}
	}
}
