// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"fmt"
	"math/rand"
	"node-agent/util"
	"os"
	"os/exec"
	"os/user"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

func waitForChildPid(pidFile string, timeout time.Duration) (int, error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		data, err := os.ReadFile(pidFile)
		if err == nil {
			pidStr := strings.TrimSpace(string(data))
			if pidStr != "" {
				pid, err := strconv.Atoi(pidStr)
				if err == nil {
					return pid, nil
				}
			}
		}
		time.Sleep(20 * time.Millisecond)
	}
	return 0, fmt.Errorf("Timed out waiting for pid file %s", pidFile)
}

func isProcessRunning(pid int) bool {
	proc, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	return proc.Signal(syscall.Signal(0)) == nil
}

func TestRunCommandExitsNormally(t *testing.T) {
	ctx := context.Background()
	cmd := exec.Command("bash", "-c", "echo ok")
	err := runCommand(ctx, cmd)
	if err != nil {
		t.Fatalf("runCommand failed: %v", err)
	}
}

func TestRunCommandKillsProcessGroupOnCancel(t *testing.T) {
	pidFile := fmt.Sprintf("%s/child-pid", t.TempDir())
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// Start a background process, write the PID to a file and wait for it to exit.
	cmd := exec.Command(
		"bash",
		"-c",
		fmt.Sprintf("sleep 300 & echo $! > %q; wait", pidFile),
	)
	errCh := make(chan error, 1)
	go func() {
		// Run the command in the background and send the error to the channel.
		errCh <- runCommand(ctx, cmd)
	}()

	childPid, err := waitForChildPid(pidFile, 5*time.Second)
	if err != nil {
		cancel()
		t.Fatalf("Child process did not start: %v", err)
	}
	if !isProcessRunning(childPid) {
		t.Fatalf("Child process %d is not running before cancel", childPid)
	}

	// Cancel the context to kill the process group.
	cancel()
	select {
	case err := <-errCh:
		if err == nil {
			t.Fatal("Expected non-nil error after cancel killed the process group")
		}
	case <-time.After(processGroupKillGracePeriod + 3*time.Second):
		t.Fatal("runCommand did not return after context cancel")
	}

	// Make sure child process is killed.
	if isProcessRunning(childPid) {
		t.Fatalf("Child process %d still running after cancel", childPid)
	}
}

func TestCommandEnv(t *testing.T) {
	ctx := context.Background()
	currentUser, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}
	userDetail, err := util.UserInfo(currentUser.Username)
	if err != nil {
		t.Fatal(err)
	}
	env, err := userEnv(ctx, userDetail)
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("Env: %v", env)
	if len(env) == 0 {
		t.Fatalf("At least one env var is expected")
	}
}

func TestRedactOutput(t *testing.T) {
	input := "--max_log_size=256\n" +
		"--ycql_ldap_bind_passwd=dummy-ycql-secret\n" +
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ` +
		`ldapbindpasswd=""dummy hba,secret"" ldapsearchattribute=uid",host all all ::0/0 md5` +
		"\r\n--placement_cloud=gcp\n"
	output := RedactOutput(input)
	for _, secret := range []string{"dummy-ycql-secret", "dummy hba", "secret\"\""} {
		if strings.Contains(output, secret) {
			t.Fatalf("Output contains %q: %s", secret, output)
		}
	}
	expected := "--max_log_size=256\n" +
		"--ycql_ldap_bind_passwd=REDACTED\n" +
		`--ysql_hba_conf_csv="host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ` +
		"ldapbindpasswd=REDACTED\r\n--placement_cloud=gcp\n"
	if output != expected {
		t.Fatalf("Expected %q, got %q", expected, output)
	}
}

func TestRedactOutputMatchesRegexp(t *testing.T) {
	reference := regexp.MustCompile(`((?:ycql_ldap_bind_passwd|ldapbindpasswd)\s*=)[^\r\n]*`)
	tokens := []string{
		"ldapbindpasswd", "ycql_ldap_bind_passwd", "passwd", "ldapbind", "ycql_ldap_bind_",
		"LDAPBINDPASSWD", "=", " ", "\t", "\f", "\n", "\r", "\r\n", `"`, ",", "x", "--",
	}
	rnd := rand.New(rand.NewSource(1))
	for i := 0; i < 20000; i++ {
		var sb strings.Builder
		for n := rnd.Intn(30); n > 0; n-- {
			sb.WriteString(tokens[rnd.Intn(len(tokens))])
		}
		input := sb.String()
		expected := reference.ReplaceAllString(input, "${1}REDACTED")
		if output := RedactOutput(input); output != expected {
			t.Fatalf("Input %q: expected %q, got %q", input, expected, output)
		}
	}
}

func BenchmarkRedactOutput(b *testing.B) {
	inputs := map[string]string{
		"no_ldap_options": strings.Repeat(
			"drwxr-xr-x 2 yugabyte yugabyte 4096 data-000123.sst\n",
			20000,
		),
		"secret_every_line": strings.Repeat(
			"--ycql_ldap_bind_passwd=dummy\n"+
				"--ysql_hba_conf_csv=\"host all all 0/0 ldap ldapbindpasswd=dummy\"\n",
			10000,
		),
	}
	for name, input := range inputs {
		b.Run(name, func(b *testing.B) {
			b.SetBytes(int64(len(input)))
			for i := 0; i < b.N; i++ {
				RedactOutput(input)
			}
		})
	}
}

func TestChildProcessHasPath(t *testing.T) {
	if os.Getenv("PATH") == "" {
		t.Skip("PATH is not set in the test runner environment")
	}
	os.Setenv("PATH", "MY_PARENT_PATH:"+os.Getenv("PATH"))
	t.Logf("PATH: %s", os.Getenv("PATH"))
	ctx := context.Background()
	cmdInfo, err := RunShellCmd(
		ctx,
		"",
		"check_path",
		`test -n "$PATH" && printenv PATH`,
		nil,
	)
	if err != nil {
		t.Fatalf("Command failed: %s, stderr: %s", err, cmdInfo.StdErr.String())
	}

	path := strings.TrimSpace(cmdInfo.StdOut.String())
	if path == "" {
		t.Fatal("Child process printed empty PATH")
	}
	if !strings.Contains(path, "MY_PARENT_PATH") {
		t.Fatalf("Child process PATH does not contain MY_PATH: %s", path)
	}
}
