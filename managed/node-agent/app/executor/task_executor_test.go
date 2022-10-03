// Copyright (c) YugaByte, Inc.

package executor

import (
	"context"
	"fmt"
	"testing"
	"time"
)

func testHandler(ctx context.Context) (any, error) {
	return "test", nil
}

func testHandlerSlowTask(ctx context.Context) (any, error) {
	time.Sleep(10 * time.Second)
	return "test", nil
}

func testHandlerFailure(ctx context.Context) (any, error) {
	return nil, fmt.Errorf("error")
}

func TestExecutor(t *testing.T) {
	ctx := context.Background()
	instance := GetInstance(ctx)
	future, err := instance.SubmitTask(ctx, testHandler)
	if err != nil {
		t.Errorf("Submitting task to the executor failed - %s", err.Error())
	}

	data, err := future.Get()
	if err != nil {
		t.Errorf("Future.Get() failed - %s", err.Error())
	}
	data, ok := data.(string)
	if !ok {
		t.Errorf("Future.Get() returned incorrect data - %s", err.Error())
	}

	if data != "test" {
		t.Errorf("Result assertion failed.")
	}
}

func TestExecutorFailure(t *testing.T) {
	ctx := context.Background()
	instance := GetInstance(ctx)
	future, err := instance.SubmitTask(ctx, testHandlerFailure)
	if err != nil {
		t.Errorf("Submitting task to the executor failed - %s", err.Error())
	}
	_, err = future.Get()
	if err == nil {
		t.Errorf("Expected Failure")
	}
}

func TestExecutorCancel(t *testing.T) {
	ctx, cancelFunc := context.WithCancel(context.Background())
	instance := GetInstance(ctx)

	future, err := instance.SubmitTask(ctx, testHandlerSlowTask)
	if err != nil {
		t.Errorf("Submitting task to the executor failed - %s", err.Error())
	}
	cancelFunc()
	_, err = future.Get()
	if err == nil {
		t.Errorf("Expected Failure")
	}

	if err.Error() != "Task is cancelled" {
		t.Errorf("Expected Canceled status")
	}
}
