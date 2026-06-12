// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"node-agent/util"
	"os"
	"os/user"
	"strings"
	"testing"
)

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
