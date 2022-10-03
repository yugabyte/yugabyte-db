// Copyright (c) YugaByte, Inc.

package executor

import (
	"context"
	"errors"
	"fmt"
	"node-agent/util"
	"sync"
)

// Future is a struct for facilitating async tasks.
type Future struct {
	ch   chan struct{}
	data any
	err  error
}

func (f *Future) Done() <-chan struct{} {
	return f.ch
}

func (f *Future) Get() (any, error) {
	<-f.ch
	return f.data, f.err
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

// SubmitTask wraps a task in asyncTask and assigns the
// async task to a goroutine. It returns a Future.
func (te *TaskExecutor) SubmitTask(ctx context.Context, handler util.Handler) (*Future, error) {
	if te.isShutdown() {
		return nil, fmt.Errorf("TaskExecutor is shutdown")
	}
	te.wg.Add(1)
	future := &Future{ch: make(chan struct{})}
	go func() {
		defer close(future.ch)
		defer te.wg.Done()
		select {
		// TaskExecutor level context.
		case <-te.ctx.Done():
			future.err = errors.New("TaskExecutor is shutdown")
		// Task level context.
		case <-ctx.Done():
			future.err = errors.New("Task is cancelled")
		default:
			future.data, future.err = handler(ctx)
		}
	}()
	return future, nil
}

// Submits a task and waits for completion.
func (te *TaskExecutor) ExecuteTask(ctx context.Context, handler util.Handler) error {
	future, err := te.SubmitTask(ctx, handler)
	if err != nil {
		return fmt.Errorf("Failed to submit the task. Error: %s", err.Error())
	}
	_, err = future.Get()
	if err != nil {
		return fmt.Errorf("Error in executing the task. Error: %s", err.Error())
	}
	return nil
}
