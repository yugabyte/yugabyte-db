// Copyright (c) YugabyteDB, Inc.

package executor

import (
	"context"
	"errors"
	"fmt"
	"node-agent/util"
	"runtime/debug"
	"sync"
	"sync/atomic"
)

// TaskState represents state of a task.
type TaskState string

func (state TaskState) String() string {
	return string(state)
}

const (
	// TaskScheduled indicates the task is scheduled for running.
	TaskScheduled TaskState = "Scheduled"
	// TaskRunning indicates the task is running.
	TaskRunning TaskState = "Running"
	// TaskAborted indicates the task is aborted.
	TaskAborted TaskState = "Aborted"
	// TaskFailed indicates the task has failed.
	TaskFailed TaskState = "Failed"
	// TaskSuccess indicates the task has succeeded.
	TaskSuccess TaskState = "Success"
)

// Future is a struct for facilitating async tasks.
type Future struct {
	ch    chan struct{}
	data  any
	err   error
	state *atomic.Value
}

// Done returns a channel to check if the task is completed.
func (f *Future) Done() <-chan struct{} {
	return f.ch
}

// State returns the state of the submitted task.
func (f *Future) State() TaskState {
	return f.state.Load().(TaskState)
}

// Get wait for the task to complete and returns the result.
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

// Init creates the singleton task executor.
func Init(ctx context.Context) *TaskExecutor {
	once.Do(func() {
		instance = &TaskExecutor{wg: &sync.WaitGroup{}, ctx: ctx}
	})
	return instance
}

// GetInstance returns the singleton executor instance.
func GetInstance() *TaskExecutor {
	if instance == nil {
		util.FileLogger().Fatal(context.TODO(), "Task executor is not initialized")
	}
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
func (te *TaskExecutor) SubmitTask(
	ctx context.Context,
	handler util.Handler,
) (*Future, error) {
	if te.isShutdown() {
		return nil, fmt.Errorf("TaskExecutor is shutdown")
	}
	te.wg.Add(1)
	future := &Future{ch: make(chan struct{}), state: &atomic.Value{}}
	future.state.Store(TaskScheduled)
	go func() {
		future.state.Store(TaskRunning)
		defer func() {
			te.wg.Done()
			if err := recover(); err != nil {
				util.FileLogger().Errorf(ctx, "Panic occurred: %v", string(debug.Stack()))
				if future.state.CompareAndSwap(TaskRunning, TaskFailed) {
					future.err = fmt.Errorf("Panic occurred: %v", err)
					close(future.ch)
				}
			}
		}()
		go func() {
			// Monitor for completion or cancellation.
			select {
			case <-future.Done():
			// TaskExecutor level context.
			case <-te.ctx.Done():
				if future.state.CompareAndSwap(TaskRunning, TaskAborted) {
					future.err = errors.New("TaskExecutor is shutdown")
					close(future.ch)
				}
			// Task level context.
			case <-ctx.Done():
				if future.state.CompareAndSwap(TaskRunning, TaskAborted) {
					util.FileLogger().Errorf(ctx, "Task is cancelled")
					future.err = errors.New("Task is cancelled")
					close(future.ch)
				}
			}
		}()
		data, err := handler(ctx)
		state := TaskSuccess
		if err != nil {
			state = TaskFailed
		}
		if future.state.CompareAndSwap(TaskRunning, state) {
			future.data = data
			future.err = err
			close(future.ch)
		}
	}()
	return future, nil
}

// Submits a task and waits for completion.
func (te *TaskExecutor) ExecuteTask(ctx context.Context, handler util.Handler) error {
	future, err := te.SubmitTask(ctx, handler)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Failed to submit the task. Error %s", err.Error())
		return err
	}
	_, err = future.Get()
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in executing the task. Error: %s", err.Error())
		return err
	}
	return nil
}
