/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import "testing"

func TestConfig(t *testing.T) {
	config := CurrentConfig()
	want, got := "nodeName", config.String(NodeNameKey)
	if want != got {
		t.Fatalf("Expected %s but got %s", want, got)
	}
	config.Update("test1.test2", "value2")
	config.Remove("test1.test2")
	want, got = "", config.String("test1.test2")
	if want != got {
		t.Fatalf("Expected %s but got %s", want, got)
	}
}
