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
		t.Errorf("Error while running shell task")
	}

	if result != "test\n" {
		t.Errorf("Unexpected result")
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
		t.Errorf("Error while running preflight checks test")
	}
	data, ok := result.(map[string]model.PreflightCheckVal)
	if !ok {
		t.Errorf("Did not receive expected result map")
		return
	}

	if _, ok := data["ports:54422"]; !ok {
		t.Errorf("Did not find expected key in the result map")
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
		t.Errorf("Incorrect preflight run options. ")
	}

}
