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
	Init(ctx)
	instance := GetInstance()
	t.Logf("Submitting task...")
	future, err := instance.SubmitTask(ctx, testHandler)
	if err != nil {
		t.Fatalf("Submitting task to the executor failed - %s", err.Error())
	}
	t.Logf("Executed")
	data, err := future.Get()
	if err != nil {
		t.Fatalf("Future.Get() failed - %s", err.Error())
	}
	data, ok := data.(string)
	if !ok {
		t.Fatalf("Future.Get() returned incorrect data - %s", err.Error())
	}

	if data != "test" {
		t.Fatalf("Result assertion failed.")
	}
	if future.State() != TaskSuccess {
		t.Fatalf("Expected task state %s, found %s", TaskSuccess, future.State())
	}
}

func TestExecutorFailure(t *testing.T) {
	ctx := context.Background()
	Init(ctx)
	instance := GetInstance()
	future, err := instance.SubmitTask(ctx, testHandlerFailure)
	if err != nil {
		t.Fatalf("Submitting task to the executor failed - %s", err.Error())
	}
	_, err = future.Get()
	if err == nil {
		t.Fatalf("Expected Failure")
	}
	if future.State() != TaskFailed {
		t.Fatalf("Expected task state %s, found %s", TaskFailed, future.State())
	}
}

func TestExecutorCancel(t *testing.T) {
	ctx, cancelFunc := context.WithCancel(context.Background())
	Init(ctx)
	instance := GetInstance()
	future, err := instance.SubmitTask(ctx, testHandlerSlowTask)
	if err != nil {
		t.Fatalf("Submitting task to the executor failed - %s", err.Error())
	}
	cancelFunc()
	_, err = future.Get()
	if err == nil {
		t.Fatalf("Expected Failure")
	}

	if err.Error() != "Task is cancelled" {
		t.Fatalf("Expected Canceled status")
	}
	if future.State() != TaskAborted {
		t.Fatalf("Expected task state %s, found %s", TaskAborted, future.State())
	}
}

func TestExecutorPanic(t *testing.T) {
	ctx := context.Background()
	Init(ctx)
	instance := GetInstance()
	future, err := instance.SubmitTask(ctx, func(ctx context.Context) (any, error) {
		panic("Panic")
	})
	if err != nil {
		t.Fatalf("Submitting task to the executor failed - %s", err.Error())
	}
	_, err = future.Get()
	if err == nil {
		t.Fatalf("Expected Failure")
	}
	if future.State() != TaskFailed {
		t.Fatalf("Expected task state %s, found %s", TaskFailed, future.State())
	}
}
