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

// runInstaller runs node-agent-installer.sh install with stubbed id, curl and systemctl, and
// returns the arguments the installer passed to node-agent node configure. It runs in dir, so
// files there let an unquoted * glob to something visible. With disableEgress, it runs the
// path YBA uses: the package and cert dir are staged on the node instead of downloaded.
func runInstaller(t *testing.T, dir string, disableEgress bool, extraArgs ...string) []string {
	t.Helper()
	bash, err := exec.LookPath("bash")
	if err != nil {
		t.Skip("bash is required to run the installer")
	}
	installer := resourcePath(t, "node-agent-installer.sh")
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

	args := []string{installer, "-c", "install",
		"--node_name", "node-1", "--node_ip", "10.0.0.1", "--install_path", home}
	if disableEgress {
		certDir := "certs-1"
		releaseDir := filepath.Join(home, "node-agent", "release")
		for _, d := range []string{releaseDir, filepath.Join(home, "node-agent", "cert", certDir)} {
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatalf("create %s: %v", d, err)
			}
		}
		writeNodeAgentPackage(t, filepath.Join(releaseDir, "node-agent.tgz"), argsFile)
		args = append(args, "--disable_egress", "--id", "node-agent-1",
			"--customer_id", "customer-1", "--cert_dir", certDir)
	} else {
		args = append(args, "-u", "http://yba:9000", "-t", "api-token", "--silent",
			"--provider_id", "provider-123", "--instance_type", "c5.large",
			"--region_name", "region-a", "--zone_name", "zone-a")
	}
	args = append(args, extraArgs...)
	cmd := exec.Command(bash, args...)
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
	data, err := os.ReadFile(argsFile)
	if err != nil {
		t.Fatalf("node-agent was not called: %v", err)
	}
	return strings.Split(strings.TrimSuffix(string(data), "\n"), "\n")
}

// The installer must hand the certificate name to node-agent as exactly one argument, without
// splitting or globbing it.
func TestInstallerCertificateNameIsOneArgument(t *testing.T) {
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
			args := runInstaller(t, dir, false, "--skip_verify_cert", "--certificate_name", label)
			for _, name := range []string{"m1", "m2"} {
				if _, err := os.Stat(filepath.Join(dir, name)); err == nil {
					t.Fatalf("label %q ran shell code: %s was created", label, name)
				}
			}
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

// Optional flags must reach node-agent as exact flags. node-agent node configure accepts stray
// positional arguments, so a padded " --skip_verify_cert" or an empty argument is silently
// ignored instead of failing.
func TestInstallerOptionalFlags(t *testing.T) {
	tests := []struct {
		name          string
		disableEgress bool
		extraArgs     []string
		wantSkip      bool
	}{
		{"skip_verify_cert", false, []string{"--skip_verify_cert"}, true},
		{"verify_cert", false, nil, false},
		{"disable_egress_skip_verify_cert", true, []string{"--skip_verify_cert"}, true},
		{"disable_egress_verify_cert", true, nil, false},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			args := runInstaller(t, t.TempDir(), tc.disableEgress, tc.extraArgs...)
			gotSkip, gotDisableEgress := false, false
			for _, arg := range args {
				if arg == "" || arg != strings.TrimSpace(arg) {
					t.Fatalf("node-agent args = %q, has empty or padded argument %q", args, arg)
				}
				switch arg {
				case "--skip_verify_cert":
					gotSkip = true
				case "--disable_egress":
					gotDisableEgress = true
				}
			}
			if gotDisableEgress != tc.disableEgress {
				t.Fatalf("node-agent args = %q, --disable_egress present = %v, want %v",
					args, gotDisableEgress, tc.disableEgress)
			}
			if gotSkip != tc.wantSkip {
				t.Fatalf("node-agent args = %q, --skip_verify_cert present = %v, want %v",
					args, gotSkip, tc.wantSkip)
			}
		})
	}
}
