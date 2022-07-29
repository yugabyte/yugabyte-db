/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import "testing"

func TestConfig(t *testing.T) {
	config, err := GetTestConfig()
	if err != nil {
		t.Errorf("Error while loading test config")
	}
	want, got := "nodeName", config.GetString(NodeName)
	if want != got {
		t.Errorf("Expected %s but got %s", want, got)
	}
}
