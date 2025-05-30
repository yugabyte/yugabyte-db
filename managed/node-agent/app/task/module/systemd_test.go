// Copyright (c) YugaByte, Inc.

package module

import (
	"testing"
)

func TestControlServerCmd(t *testing.T) {
	cmd, err := ControlServerCmd("", "yb-master", "start")
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("Cmd: %v", cmd)
	expected := "systemctl daemon-reload && systemctl enable yb-master && systemctl start yb-master"
	if cmd != expected {
		t.Fatalf("Expected: %s, got: %s", expected, cmd)
	}
}
