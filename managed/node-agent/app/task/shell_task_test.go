/*
 * Copyright (c) YugaByte, Inc.
 */
package task

import (
	"context"
	"node-agent/model"
	"node-agent/util"
	"testing"
)

func TestShellTaskProcess(t *testing.T) {
	util.GetTestConfig()
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
	util.GetTestConfig()
	dummyInstanceType := util.GetTestInstanceTypeData()
	handler := HandlePreflightCheck(dummyInstanceType)
	ctx := context.Background()
	result, err := handler(ctx)
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
	dummyInstanceType := util.GetTestInstanceTypeData()
	result := getOptions("./dummy.sh", dummyInstanceType)
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
