// Copyright (c) YugabyteDB, Inc.

package nodeagent

import (
	"context"
	"fmt"
	"node-agent/app/task/module"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// resourcePath returns a path under managed/node-agent/resources.
func resourcePath(t *testing.T, elems ...string) string {
	t.Helper()
	projectDir := os.Getenv("PROJECT_DIR")
	if projectDir == "" {
		var err error
		projectDir, err = filepath.Abs(filepath.Join("..", "..", "..", ".."))
		if err != nil {
			t.Fatalf("resolve project dir: %v", err)
		}
	}
	return filepath.Join(append([]string{projectDir, "resources"}, elems...)...)
}

func runTemplatePath(t *testing.T) string {
	return resourcePath(t, "ynp", "modules", "provision", "node_agent", "templates", "run.j2")
}

func writeExecutable(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatalf("create directory for %s: %v", path, err)
	}
	if err := os.WriteFile(path, []byte(content), 0o755); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// The certificate name is a free-form YBA label. It must reach the installer as exactly one
// argument, through both the non-root call and the root call that goes through su -c, and shell
// syntax in it must never run.
func TestRunTemplateCertificateNameIsOneArgument(t *testing.T) {
	bash, err := exec.LookPath("bash")
	if err != nil {
		t.Skip("bash is required to run the rendered script")
	}
	labels := []string{
		"",
		"na cert x",
		"na#x",
		"na-x;touch m1",
		"na-x$(touch m2)",
		"na-x`touch m3`",
		"it's",
		`a"b\c`,
	}
	for _, sudo := range []bool{false, true} {
		for i, label := range labels {
			t.Run(fmt.Sprintf("sudo=%t/%d", sudo, i), func(t *testing.T) {
				dir := t.TempDir()
				argsFile := filepath.Join(dir, "installer-args")
				// run.j2 looks for the installer at {{ ynp_dir }}/../../bin.
				ynpDir := filepath.Join(dir, "ynp", "scripts")
				if err := os.MkdirAll(ynpDir, 0o755); err != nil {
					t.Fatalf("create %s: %v", ynpDir, err)
				}
				writeExecutable(t, filepath.Join(dir, "bin", "node-agent-installer.sh"),
					fmt.Sprintf("#!/bin/bash\nprintf '%%s\\n' \"$@\" > '%s'\n", argsFile))
				stubDir := filepath.Join(dir, "stubs")
				// Stand-ins for root-only commands: su - <user> -c <cmd> runs <cmd> in a new shell.
				writeExecutable(
					t,
					filepath.Join(stubDir, "su"),
					"#!/bin/bash\nexec bash -c \"$4\"\n",
				)
				writeExecutable(t, filepath.Join(stubDir, "chown"), "#!/bin/bash\nexit 0\n")
				writeExecutable(t, filepath.Join(stubDir, "systemctl"), "#!/bin/bash\nexit 0\n")

				values := map[string]any{
					"url":                       "http://yba:9000",
					"customer_uuid":             "customer-123",
					"api_key":                   "api-key",
					"provider_id":               "provider-123",
					"tmp_directory":             filepath.Join(dir, "tmp"),
					"is_airgap":                 "False",
					"configure_fips":            "False",
					"ynp_dir":                   ynpDir,
					"yb_home_dir":               filepath.Join(dir, "home"),
					"yb_user":                   "yugabyte",
					"node_ip":                   "10.0.0.1",
					"node_agent_port":           "9070",
					"instance_type_name":        "c5.large",
					"provider_region_name":      "region-a",
					"provider_region_zone_name": "zone-a",
					"node_name":                 "node-1",
					"node_external_fqdn":        "10.0.0.1",
					"bind_ip":                   "10.0.0.1",
					"certificate_name":          label,
				}
				script, err := module.ResolveTemplateStrict(
					context.Background(), values, runTemplatePath(t), true /*strictUndefined*/)
				if err != nil {
					t.Fatalf("ResolveTemplateStrict() error = %v", err)
				}
				scriptPath := filepath.Join(dir, "run.sh")
				writeExecutable(t, scriptPath, script)

				cmd := exec.Command(bash, scriptPath)
				cmd.Dir = dir
				cmd.Env = append(os.Environ(),
					"PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"),
					fmt.Sprintf("SUDO_ACCESS=%t", sudo))
				if out, err := cmd.CombinedOutput(); err != nil {
					t.Fatalf("run.sh failed: %v\n%s", err, out)
				}

				for _, name := range []string{"m1", "m2", "m3"} {
					if _, err := os.Stat(filepath.Join(dir, name)); err == nil {
						t.Fatalf("label %q ran shell code: %s was created", label, name)
					}
				}
				data, err := os.ReadFile(argsFile)
				if err != nil {
					t.Fatalf("installer was not called: %v", err)
				}
				args := strings.Split(strings.TrimSuffix(string(data), "\n"), "\n")
				if label == "" {
					if slices.Contains(args, "--certificate_name") || args[len(args)-1] == "" {
						t.Fatalf("installer args = %q, want no certificate name", args)
					}
					return
				}
				for j, arg := range args {
					if arg == "--certificate_name" {
						if j+1 >= len(args) || args[j+1] != label {
							t.Fatalf("installer args = %q, want --certificate_name %q", args, label)
						}
						if j+2 != len(args) {
							t.Fatalf("installer args = %q, label split into extra arguments", args)
						}
						return
					}
				}
				t.Fatalf("installer args = %q, missing --certificate_name", args)
			})
		}
	}
}
