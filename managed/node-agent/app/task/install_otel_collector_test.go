// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	pb "node-agent/generated/service"
	"os"
	"os/user"
	"path/filepath"
	"testing"
)

func TestSystemdEnvQuote(t *testing.T) {
	cases := map[string]string{
		"plain":          `"plain"`,
		`a"b`:            `"a\"b"`,
		`a\b`:            `"a\\b"`,
		"a$b`c":          "\"a\\$b\\`c\"",
		"Basic dXNlcjpw": `"Basic dXNlcjpw"`,
	}
	for in, want := range cases {
		if got := systemdEnvQuote(in); got != want {
			t.Errorf("systemdEnvQuote(%q) = %s, want %s", in, got, want)
		}
	}
}

func TestWriteSecretEnvFile(t *testing.T) {
	currentUser, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}
	h := NewInstallOtelCollectorHandler(&pb.InstallOtelCollectorInput{
		OtelColSecretEnv: map[string]string{
			"YB_OTEL_B_DD_API_KEY":   "p'1%s$(id)`id`\\\"",
			"YB_OTEL_A_SPLUNK_TOKEN": "tok",
		},
	}, currentUser.Username)
	path := filepath.Join(t.TempDir(), otelCollectorSecretEnvFile)
	if err := h.writeSecretEnvFile(context.Background(), path); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0600 {
		t.Errorf("mode = %v, want 0600", info.Mode().Perm())
	}
	content, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	want := "YB_OTEL_A_SPLUNK_TOKEN=\"tok\"\n" +
		"YB_OTEL_B_DD_API_KEY=\"p'1%s\\$(id)\\`id\\`\\\\\\\"\"\n"
	if string(content) != want {
		t.Errorf("content = %q, want %q", content, want)
	}
}

func TestCheckCredentialsRejectsLineBreak(t *testing.T) {
	cases := map[string]*pb.InstallOtelCollectorInput{
		"secret env": {OtelColSecretEnv: map[string]string{"YB_OTEL_A_DD_API_KEY": "key\n"}},
		"aws secret": {OtelColAwsAccessKey: "AKIA", OtelColAwsSecretKey: "x\rcredential_process=id"},
	}
	for name, input := range cases {
		if err := NewInstallOtelCollectorHandler(input, "").checkCredentials(); err == nil {
			t.Errorf("%s: want an error for a line break, got nil", name)
		}
	}
	valid := &pb.InstallOtelCollectorInput{
		OtelColSecretEnv:    map[string]string{"YB_OTEL_A_DD_API_KEY": "key$1"},
		OtelColAwsAccessKey: "AKIA",
		OtelColAwsSecretKey: "se'cret",
	}
	if err := NewInstallOtelCollectorHandler(valid, "").checkCredentials(); err != nil {
		t.Errorf("valid credentials: %v", err)
	}
}

func TestAppendAwsCredsBlock(t *testing.T) {
	currentUser, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}
	h := NewInstallOtelCollectorHandler(&pb.InstallOtelCollectorInput{
		OtelColAwsAccessKey: "AKIA'1%s",
		OtelColAwsSecretKey: "se'cret$(id)`id`\\",
	}, currentUser.Username)
	path := filepath.Join(t.TempDir(), "credentials")
	if err := os.WriteFile(path, []byte("[default]\n"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := h.appendAwsCredsBlock(context.Background(), path); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0440 {
		t.Errorf("mode = %v, want 0440", info.Mode().Perm())
	}
	content, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	want := "[default]\n" +
		"# BEGIN YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS\n" +
		"[otel-collector]\n" +
		"aws_access_key_id = AKIA'1%s\n" +
		"aws_secret_access_key = se'cret$(id)`id`\\\n" +
		"# END YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS\n"
	if string(content) != want {
		t.Errorf("content = %q, want %q", content, want)
	}
}
