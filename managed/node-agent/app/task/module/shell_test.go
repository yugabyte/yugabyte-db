// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"fmt"
	"node-agent/util"
	"os"
	"os/exec"
	"os/user"
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
