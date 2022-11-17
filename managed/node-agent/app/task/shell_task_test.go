// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"node-agent/model"
	"node-agent/util"
	"testing"
)

func TestShellTaskProcess(t *testing.T) {
	testShellTask := NewShellTask("test_echo", "echo", []string{"test"})
	ctx := context.Background()
	result, err := testShellTask.Process(ctx)
	if err != nil {
		t.Fatalf("Error while running shell task - %s", err.Error())
	}

	if result != "test\n" {
		t.Fatalf("Unexpected result")
	}
}

func TestPreflightCheckTask(t *testing.T) {
	providerData := util.GetTestProviderData()
	accessKeyData := util.GetTestAccessKeyData()
	dummyInstanceType := util.GetTestInstanceTypeData()
	handler := NewPreflightCheckHandler(&providerData, &dummyInstanceType, &accessKeyData)
	ctx := context.Background()
	result, err := handler.Handle(ctx)
	if err != nil {
		t.Fatalf("Error while running preflight checks test - %s", err.Error())
	}
	data, ok := result.(map[string]model.PreflightCheckVal)
	if !ok {
		t.Fatalf("Did not receive expected result map")
	}

	if _, ok := data["ports:54422"]; !ok {
		t.Fatalf("Did not find expected key in the result map")
	}
}

func TestGetOptions(t *testing.T) {
	providerData := util.GetTestProviderData()
	accessKeyData := util.GetTestAccessKeyData()
	dummyInstanceType := util.GetTestInstanceTypeData()
	handler := NewPreflightCheckHandler(&providerData, &dummyInstanceType, &accessKeyData)
	result := handler.getOptions("./dummy.sh")
	check := false
	for i, v := range result {
		if v == "--ports_to_check" && result[i+1] == "54422" {
			check = true
		}
	}
	if !check {
		t.Fatalf("Incorrect preflight run options. ")
	}

}
