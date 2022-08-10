// Copyright (c) YugaByte, Inc.

package executor

import (
	"context"
	"fmt"
	"node-agent/util"
	"sync"
)

// Future is a struct for facilitating async tasks.
type Future struct {
	done       bool
	responseWg *sync.WaitGroup
	data       *Result
}

func (f *Future) Get() *Result {
	f.responseWg.Wait()
	f.done = true
	return f.data
}

func (f *Future) IsDone() bool {
	return f.done
}

// This is a wrapper for handler that allows any handler to
// run asynchronously.
type asyncTask struct {
	taskHandle   util.Handler
	futureHandle *Future
}

var (
	instance *TaskExecutor
	once     = &sync.Once{}
)

// Task Executor manages and runs the tasks submitted to it
// by assigning each task to a goroutine.
type TaskExecutor struct {
	wg  *sync.WaitGroup
	ctx context.Context
}

// Making the task Executor singleton to make
// the startup and shutdown easier.
func GetInstance(ctx context.Context) *TaskExecutor {
	once.Do(func() {
		instance = &TaskExecutor{wg: &sync.WaitGroup{}, ctx: ctx}
	})
	return instance
}

func (te *TaskExecutor) isShutdown() bool {
	select {
	case <-te.ctx.Done():
		return true
	default:
		return false
	}
}

// Task Executor waits for all the threads to complete execution
// and does not accept new tasks.
func (te *TaskExecutor) WaitOnShutdown() {
	if !te.isShutdown() {
		panic("Shutdown is not issued")
	}
	te.wg.Wait()
}

// Runs the task handler and puts the result in Future.data.
// Listens for the ctx.cancel signal to cancel the task at
// task executor level as well as the task level.
func (te *TaskExecutor) runTask(ctx context.Context, asyncTask asyncTask) {
	defer te.wg.Done()
	defer asyncTask.futureHandle.responseWg.Done()
	var result *Result
	select {
	//TaskExecutor level context.
	case <-te.ctx.Done():
		result = NewResult(fmt.Errorf("TaskExecutor is closed"), "canceled", nil)
	//Task level context
	case <-ctx.Done():
		result = NewResult(fmt.Errorf("Task cancelled"), "canceled", nil)
	default:
		response, err := asyncTask.taskHandle(ctx)
		if err != nil {
			result = NewResult(err, "error", response)
		} else {
			result = NewResult(nil, "success", response)
		}
	}
	asyncTask.futureHandle.data = result
}

// SubmitTask wraps a task in asyncTask and assigns the
// async task to a goroutine. It returns a Future.
func (te *TaskExecutor) SubmitTask(ctx context.Context, handler util.Handler) (*Future, error) {
	if te.isShutdown() {
		return nil, fmt.Errorf("TaskExecutor is closed")
	}
	te.wg.Add(1)
	futureHandler := &Future{responseWg: &sync.WaitGroup{}}
	futureHandler.responseWg.Add(1)
	futureTask := asyncTask{taskHandle: handler, futureHandle: futureHandler}
	go te.runTask(ctx, futureTask)
	return futureTask.futureHandle, nil
}

// Submits a task and waits for completion.
func (te *TaskExecutor) ExecuteTask(ctx context.Context, handler util.Handler) (any, error) {
	future, err := te.SubmitTask(ctx, handler)
	if err != nil {
		return nil, fmt.Errorf("Failed to submit the task. Error: %s", err.Error())
	}
	result := future.Get()
	if result.Err() != nil {
		//result.data might return nil. The caller needs to nil check.
		return result.data, fmt.Errorf("Error in executing the task. Error: %s", result.Err())
	}
	return result.data, nil
}
