package common

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

func TestCertificateGeneration(t *testing.T) {
	// generate a root CA cert and key
	caCert, caKey := generateCert(
		"/tmp/ca_cert.pem", "/tmp/ca_key.pem", true, 10*365*24*time.Hour, "", nil, nil)

	out := shell.Run("openssl", "x509", "-in", "/tmp/ca_cert.pem", "-text", "-noout")
	if !out.Succeeded() {
		t.Fatalf("Failed to open certificate with openssl (is openssl installed?)")
	}

	out = shell.Run("openssl", "rsa", "-in", "/tmp/ca_key.pem", "-text", "-noout")
	if !out.Succeeded() {
		t.Fatalf("Failed to open key file with openssl (is openssl installed?)")
	}

	// generate a server cert and key signed by the above root CA
	generateCert("/tmp/server_cert.pem", "/tmp/server_key.pem", false, 4*365*24*time.Hour,
		"127.0.0.1", caCert, caKey)

	out = shell.Run("openssl", "x509", "-in", "/tmp/server_cert.pem", "-text", "-noout")
	if !out.Succeeded() {
		t.Fatalf("Failed to open certificate with openssl (is openssl installed?)")
	}

	out = shell.Run("openssl", "rsa", "-in", "/tmp/server_key.pem", "-text", "-noout")
	if !out.Succeeded() {
		t.Fatalf("Failed to open key file with openssl (is openssl installed?)")
	}

	out = shell.Run("openssl", "verify", "-CAfile", "/tmp/ca_cert.pem", "/tmp/server_cert.pem")
	if !out.Succeeded() {
		t.Fatalf("Failed to open key file with openssl (is openssl installed?)")
	}
}

// The bundled JRE is the only source for keytool: an ambient one would be an unversioned binary
// the install never chose, and the BCFKS keystore has to be built by the JRE that later reads it.
func TestJavaBinaryUsesOnlyTheBundledJre(t *testing.T) {
	root := t.TempDir()
	Version = "9.9.9.9-b1"
	viper.Set("installRoot", root)

	bundledBin := filepath.Join(GetInstallerSoftwareDir(), "jdk-17.0.7+7-jre", "bin")
	if err := os.MkdirAll(bundledBin, 0o755); err != nil {
		t.Fatal(err)
	}
	bundled := filepath.Join(bundledBin, "keytool")
	if err := os.WriteFile(bundled, []byte("#!/bin/sh\nexit 0\n"), 0o755); err != nil {
		t.Fatal(err)
	}

	// An equally usable keytool on both JAVA_HOME and PATH, so the assertion is about which one
	// is chosen rather than about finding one at all.
	otherBin := t.TempDir()
	if err := os.WriteFile(filepath.Join(otherBin, "keytool"),
		[]byte("#!/bin/sh\nexit 0\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("JAVA_HOME", filepath.Dir(otherBin))
	t.Setenv("PATH", otherBin)

	got, err := javaBinary("keytool")
	if err != nil {
		t.Fatalf("javaBinary: %v", err)
	}
	if got != bundled {
		t.Fatalf("expected the bundled JRE at %s, got %s", bundled, got)
	}
}

// Without the bundled JRE this must fail naming that path, not silently fall back to whatever the
// operator's environment happens to provide.
func TestJavaBinaryFailsWithoutTheBundledJre(t *testing.T) {
	Version = "9.9.9.9-b1"
	viper.Set("installRoot", t.TempDir())

	elsewhere := t.TempDir()
	if err := os.WriteFile(filepath.Join(elsewhere, "keytool"),
		[]byte("#!/bin/sh\nexit 0\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("JAVA_HOME", filepath.Dir(elsewhere))
	t.Setenv("PATH", elsewhere)

	_, err := javaBinary("keytool")
	if err == nil {
		t.Fatal("expected an error rather than a keytool from PATH or JAVA_HOME")
	}
	for _, want := range []string{"keytool", "jdk*"} {
		if !strings.Contains(err.Error(), want) {
			t.Fatalf("error %q does not mention %q", err, want)
		}
	}
	t.Logf("operator sees: %v", err)
}
