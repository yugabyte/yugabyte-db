// Copyright (c) YugabyteDB, Inc.

package nodeagent

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// writeNodeAgentPackage writes the package the installer downloads: <version>/version_metadata.json
// and <version>/bin/node-agent, a stand-in that records its arguments one per line.
func writeNodeAgentPackage(t *testing.T, path, argsFile string) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("create %s: %v", path, err)
	}
	defer f.Close()
	gz := gzip.NewWriter(f)
	tw := tar.NewWriter(gz)
	files := []struct {
		name    string
		mode    int64
		content string
	}{
		{"./1.0.0/version_metadata.json", 0o644, "{}"},
		{
			"./1.0.0/bin/node-agent",
			0o755,
			fmt.Sprintf("#!/bin/bash\nprintf '%%s\\n' \"$@\" > '%s'\n", argsFile),
		},
	}
	for _, file := range files {
		hdr := &tar.Header{
			Name:     file.name,
			Mode:     file.mode,
			Size:     int64(len(file.content)),
			Typeflag: tar.TypeReg,
		}
		if err := tw.WriteHeader(hdr); err != nil {
			t.Fatalf("write tar header: %v", err)
		}
		if _, err := tw.Write([]byte(file.content)); err != nil {
			t.Fatalf("write tar content: %v", err)
		}
	}
	if err := tw.Close(); err != nil {
		t.Fatalf("close tar: %v", err)
	}
	if err := gz.Close(); err != nil {
		t.Fatalf("close gzip: %v", err)
	}
}

// The installer must hand the certificate name to node-agent as exactly one argument, without
// splitting or globbing it.
func TestInstallerCertificateNameIsOneArgument(t *testing.T) {
	bash, err := exec.LookPath("bash")
	if err != nil {
		t.Skip("bash is required to run the installer")
	}
	installer := resourcePath(t, "node-agent-installer.sh")
	labels := []string{
		"na cert x",
		"*",
		"na#x",
		"na-x;touch m1",
		"na-x$(touch m2)",
		"it's",
	}
	for i, label := range labels {
		t.Run(fmt.Sprint(i), func(t *testing.T) {
			dir := t.TempDir()
			home := filepath.Join(dir, "home")
			if err := os.MkdirAll(home, 0o755); err != nil {
				t.Fatalf("create %s: %v", home, err)
			}
			argsFile := filepath.Join(dir, "node-agent-args")
			pkg := filepath.Join(dir, "package.tgz")
			writeNodeAgentPackage(t, pkg, argsFile)

			stubDir := filepath.Join(dir, "stubs")
			// The installer resolves its home with ~user, which reads the user database and
			// ignores $HOME. An empty user name makes ~ resolve to $HOME, so nothing is written
			// outside the test directory.
			writeExecutable(t, filepath.Join(stubDir, "id"), `#!/bin/bash
case "$*" in
  "-u -n") echo "" ;;
  "-u"*) echo 1000 ;;
  *) exec /usr/bin/id "$@" ;;
esac
`)
			// Serves the package for the download request and reports HTTP 200.
			writeExecutable(t, filepath.Join(stubDir, "curl"), fmt.Sprintf(`#!/bin/bash
while [ $# -gt 0 ]; do
  [ "$1" = "--output" ] && out="$2"
  shift
done
cp '%s' "$out"
printf 200
`, pkg))
			writeExecutable(t, filepath.Join(stubDir, "systemctl"), "#!/bin/bash\nexit 0\n")

			cmd := exec.Command(bash, installer, "-c", "install",
				"-u", "http://yba:9000", "-t", "api-token", "--silent", "--skip_verify_cert",
				"--provider_id", "provider-123", "--instance_type", "c5.large",
				"--region_name", "region-a", "--zone_name", "zone-a",
				"--node_name", "node-1", "--node_ip", "10.0.0.1",
				"--install_path", home, "--certificate_name", label)
			// Files in the working directory let an unquoted * glob to something visible.
			cmd.Dir = dir
			cmd.Env = append(os.Environ(),
				"HOME="+home,
				"PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
			if out, err := cmd.CombinedOutput(); err != nil {
				t.Fatalf("installer failed: %v\n%s", err, out)
			}
			if _, err := os.Stat(
				filepath.Join(home, ".yugabyte", "node-agent-registry.yml"),
			); err != nil {
				t.Fatalf("installer did not write under the test home: %v", err)
			}

			for _, name := range []string{"m1", "m2"} {
				if _, err := os.Stat(filepath.Join(dir, name)); err == nil {
					t.Fatalf("label %q ran shell code: %s was created", label, name)
				}
			}
			data, err := os.ReadFile(argsFile)
			if err != nil {
				t.Fatalf("node-agent was not called: %v", err)
			}
			args := strings.Split(strings.TrimSuffix(string(data), "\n"), "\n")
			for j, arg := range args {
				if arg == "--certificate_name" {
					if j+1 >= len(args) || args[j+1] != label {
						t.Fatalf("node-agent args = %q, want --certificate_name %q", args, label)
					}
					// The next argument must be the next flag, not a piece of the label.
					if j+2 < len(args) && !strings.HasPrefix(args[j+2], "--") {
						t.Fatalf("node-agent args = %q, label split into extra arguments", args)
					}
					return
				}
			}
			t.Fatalf("node-agent args = %q, missing --certificate_name", args)
		})
	}
}
