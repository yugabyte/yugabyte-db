// Copyright (c) YugabyteDB, Inc.

package main

import "testing"

func TestIsFederationCommand(t *testing.T) {
	if !isFederationCommand([]string{"node-agent", "federation", "oci-token"}) ||
		isFederationCommand([]string{"node-agent", "server", "start"}) || isFederationCommand(nil) {
		t.Error("only the federation subcommands skip the server init")
	}
}
