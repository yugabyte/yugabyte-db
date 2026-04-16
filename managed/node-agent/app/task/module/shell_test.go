// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"node-agent/util"
	"os/user"
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
