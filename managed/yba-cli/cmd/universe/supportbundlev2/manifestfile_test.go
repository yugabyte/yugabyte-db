/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/spf13/cobra"
)

func writeDevopsManifest(t *testing.T, devopsHome string) string {
	t.Helper()
	manifestPath := filepath.Join(devopsHome, manifestPathInDevops)
	if err := os.MkdirAll(filepath.Dir(manifestPath), 0o755); err != nil {
		t.Fatalf("MkdirAll() error = %v", err)
	}
	if err := os.WriteFile(manifestPath, []byte("spec: {}\n"), 0o644); err != nil {
		t.Fatalf("WriteFile() error = %v", err)
	}
	return manifestPath
}

func newManifestFlagCommand() *cobra.Command {
	cmd := &cobra.Command{Use: "create"}
	registerSupportBundleV2ManifestFlags(cmd)
	return cmd
}

func TestResolveManifestFileFromDevopsHomeEnv(t *testing.T) {
	devopsHome := t.TempDir()
	want := writeDevopsManifest(t, devopsHome)
	t.Setenv("DEVOPS_HOME", devopsHome)

	got, err := resolveManifestFile(newManifestFlagCommand())
	if err != nil {
		t.Fatalf("resolveManifestFile() error = %v", err)
	}
	if got != want {
		t.Fatalf("resolveManifestFile() = %q, want %q", got, want)
	}
}

func TestResolveManifestFileFromYbaConf(t *testing.T) {
	devopsHome := t.TempDir()
	want := writeDevopsManifest(t, devopsHome)
	t.Setenv("DEVOPS_HOME", "")
	t.Setenv("yb_devops_home", "")

	confPath := filepath.Join(t.TempDir(), "yb-platform.conf")
	conf := "yb {\n  devops.home = \"" + devopsHome + "\"\n}\n"
	if err := os.WriteFile(confPath, []byte(conf), 0o644); err != nil {
		t.Fatalf("WriteFile() error = %v", err)
	}
	t.Setenv(ybaConfFileEnvVar, confPath)

	got, err := resolveManifestFile(newManifestFlagCommand())
	if err != nil {
		t.Fatalf("resolveManifestFile() error = %v", err)
	}
	if got != want {
		t.Fatalf("resolveManifestFile() = %q, want %q", got, want)
	}
}

func TestResolveManifestFileExplicitFlagWins(t *testing.T) {
	devopsHome := t.TempDir()
	writeDevopsManifest(t, devopsHome)
	t.Setenv("DEVOPS_HOME", devopsHome)

	explicit := filepath.Join(t.TempDir(), "custom_manifest.yaml")
	if err := os.WriteFile(explicit, []byte("spec: {}\n"), 0o644); err != nil {
		t.Fatalf("WriteFile() error = %v", err)
	}

	cmd := newManifestFlagCommand()
	if err := cmd.Flags().Set(manifestFileFlag, explicit); err != nil {
		t.Fatalf("Set() error = %v", err)
	}

	got, err := resolveManifestFile(cmd)
	if err != nil {
		t.Fatalf("resolveManifestFile() error = %v", err)
	}
	if got != explicit {
		t.Fatalf("resolveManifestFile() = %q, want %q", got, explicit)
	}
}

func TestResolveManifestFileExplicitMissingFileErrors(t *testing.T) {
	cmd := newManifestFlagCommand()
	missing := filepath.Join(t.TempDir(), "absent.yaml")
	if err := cmd.Flags().Set(manifestFileFlag, missing); err != nil {
		t.Fatalf("Set() error = %v", err)
	}

	if _, err := resolveManifestFile(cmd); err == nil {
		t.Fatal("resolveManifestFile() error = nil, want error for a missing manifest")
	}
}

func TestDevopsHomeFromYbaConf(t *testing.T) {
	tests := []struct {
		name string
		conf string
		want string
	}{
		{
			name: "yba installer flat key",
			conf: "yb.devops.home = \"/opt/yugabyte/yb-platform/devops\"\n",
			want: "/opt/yugabyte/yb-platform/devops",
		},
		{
			name: "nested yb block",
			conf: "yb {\n  storage.path = \"/opt/yugaware\"\n  devops.home = /opt/yugabyte/devops\n}\n",
			want: "/opt/yugabyte/devops",
		},
		{
			name: "trailing comment",
			conf: "yb.devops.home = \"/opt/yugabyte/devops\" # set at install time\n",
			want: "/opt/yugabyte/devops",
		},
		{
			name: "env substitution is skipped",
			conf: "yb {\n  devops.home = ${?DEVOPS_HOME}\n}\n",
			want: "",
		},
		{
			name: "commented out key is skipped",
			conf: "# yb.devops.home = \"/opt/yugabyte/devops\"\n",
			want: "",
		},
		{
			// Regression: the value used to be captured to end of line, which returned
			// `/opt/yugabyte/devops" }` for this conf.
			name: "closing brace on the same line is not part of the path",
			conf: "yb { devops.home = \"/opt/yugabyte/devops\" }\n",
			want: "/opt/yugabyte/devops",
		},
		// The three below are HOCON a real parser would resolve and a line match cannot. They must
		// yield "" so the walk moves to the next candidate: a miss ends in an error naming
		// --manifest-file, where a wrong value would silently run the wrong manifest.
		{
			name: "unsupported: nested devops block",
			conf: "yb {\n  devops {\n    home = \"/opt/yugabyte/devops\"\n  }\n}\n",
			want: "",
		},
		{
			name: "unsupported: quoted key",
			conf: "\"yb.devops.home\" = \"/opt/yugabyte/devops\"\n",
			want: "",
		},
		{
			name: "unsupported: include",
			conf: "include \"devops.conf\"\n",
			want: "",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			confPath := filepath.Join(t.TempDir(), "application.conf")
			if err := os.WriteFile(confPath, []byte(test.conf), 0o644); err != nil {
				t.Fatalf("WriteFile() error = %v", err)
			}
			if got := devopsHomeFromYbaConf(confPath); got != test.want {
				t.Fatalf("devopsHomeFromYbaConf() = %q, want %q", got, test.want)
			}
		})
	}
}

func TestDevopsHomeFromYbaConfMissingFile(t *testing.T) {
	if got := devopsHomeFromYbaConf(filepath.Join(t.TempDir(), "absent.conf")); got != "" {
		t.Fatalf("devopsHomeFromYbaConf() = %q, want empty", got)
	}
}

func TestManifestFileFlagUsageMentionsDevopsHome(t *testing.T) {
	usage := newManifestFlagCommand().Flags().Lookup(manifestFileFlag).Usage
	if !strings.Contains(usage, "yb.devops.home") {
		t.Fatalf("manifest-file usage = %q, want it to mention yb.devops.home", usage)
	}
	// YBA enforces its own manifest: it rejects a mismatched spec and supplies what runs.
	for _, want := range []string{
		"YBA rejects a component",
		"arguments, queries, timeout, tar path and Linux user from its own manifest",
	} {
		if !strings.Contains(usage, want) {
			t.Fatalf("manifest-file usage = %q, want it to say %q", usage, want)
		}
	}
}

// The manifest decides which scripts run on the nodes, with which arguments, so it must be chosen
// by what the operator installed rather than by where the command happened to be started. The
// search used to end with devops/, ../devops/ and managed/devops/ resolved against the process
// working directory, which let a planted file in a writable directory drive a collection.
func TestManifestCandidatesAreNotRelativeToWorkingDirectory(t *testing.T) {
	t.Setenv("DEVOPS_HOME", "")
	t.Setenv("yb_devops_home", "")

	for _, candidate := range defaultManifestFileCandidates() {
		if !filepath.IsAbs(candidate) {
			t.Errorf("candidate %q is relative, so it resolves against the working directory",
				candidate)
		}
	}
}

// A manifest sitting in the working directory is ignored: with nothing installed to point at, the
// command reports how to supply one rather than silently using whatever is to hand.
func TestResolveManifestFileIgnoresWorkingDirectoryManifest(t *testing.T) {
	workDir := t.TempDir()
	writeDevopsManifest(t, filepath.Join(workDir, "devops"))

	t.Chdir(workDir)
	t.Setenv("DEVOPS_HOME", "")
	t.Setenv("yb_devops_home", "")
	t.Setenv(ybaConfFileEnvVar, filepath.Join(t.TempDir(), "absent.conf"))

	got, err := resolveManifestFile(newManifestFlagCommand())
	if err == nil {
		t.Fatalf("resolveManifestFile() = %q, want an error rather than the planted manifest", got)
	}
	if !strings.Contains(err.Error(), "unable to locate") {
		t.Fatalf("error = %q, want it to explain how to supply a manifest", err.Error())
	}
}

// Narrowing the search must not cost the supported layouts: the environment still wins first.
func TestResolveManifestFileStillHonoursDevopsHomeFromWorkingDirectory(t *testing.T) {
	devopsHome := t.TempDir()
	want := writeDevopsManifest(t, devopsHome)

	t.Chdir(t.TempDir())
	t.Setenv("DEVOPS_HOME", devopsHome)

	got, err := resolveManifestFile(newManifestFlagCommand())
	if err != nil {
		t.Fatalf("resolveManifestFile() error = %v", err)
	}
	if got != want {
		t.Fatalf("resolveManifestFile() = %q, want %q", got, want)
	}
}
