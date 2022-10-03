/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import "testing"

func TestConfig(t *testing.T) {
	config := CurrentConfig()
	want, got := "nodeName", config.String(NodeNameKey)
	if want != got {
		t.Errorf("Expected %s but got %s", want, got)
	}
}
