// Copyright (c) YugaByte, Inc.

package scheduler

import (
	"context"
	"fmt"
	"node-agent/app/executor"
	"node-agent/util"
	"sync"
	"time"

	"github.com/google/uuid"
)

var (
	instance *Scheduler
	once     = &sync.Once{}
	tasks    = &sync.Map{}
)

type Scheduler struct {
	ctx      context.Context
	executor *executor.TaskExecutor
}

type taskInfo struct {
	mutex     *sync.Mutex
	id        uuid.UUID
	handler   util.Handler
	interval  time.Duration
	isRunning bool
}

func (t *taskInfo) compareAndSetRunning(state bool) bool {
	t.mutex.Lock()
	defer t.mutex.Unlock()
	if t.isRunning == state {
		return false
	}
	t.isRunning = state
	return true
}

func GetInstance(ctx context.Context) *Scheduler {
	once.Do(func() {
		instance = &Scheduler{ctx: ctx, executor: executor.GetInstance(ctx)}
	})
	return instance
}

func (s *Scheduler) executeTask(ctx context.Context, taskID uuid.UUID) error {
	value, ok := tasks.Load(taskID)
	if !ok {
		err := fmt.Errorf("Invalid state for task %s. Exiting...", taskID)
		util.FileLogger().Errorf(err.Error())
		return err
	}
	info := value.(*taskInfo)
	if !info.compareAndSetRunning(true) {
		util.FileLogger().Warnf("Task %s is still running", taskID)
		return nil
	}
	defer func() {
		info.compareAndSetRunning(false)
	}()
	err := executor.GetInstance(s.ctx).ExecuteTask(ctx, info.handler)
	if err != nil {
		tasks.Delete(taskID)
		err := fmt.Errorf("Failed to submit job %s. Error: %s", taskID, err)
		util.FileLogger().Errorf(err.Error())
		return err
	}
	return nil
}

func (s *Scheduler) Schedule(
	ctx context.Context,
	interval time.Duration,
	handler util.Handler,
) uuid.UUID {
	taskID := util.NewUUID()
	tasks.Store(
		taskID,
		&taskInfo{mutex: &sync.Mutex{}, id: taskID, interval: interval, handler: handler},
	)
	go func() {
		ticker := time.NewTicker(interval)
		for {
			select {
			case <-ctx.Done():
				return
			case <-s.ctx.Done():
				return
			case <-ticker.C:
				err := s.executeTask(ctx, taskID)
				if err != nil {
					util.FileLogger().Errorf("Exiting scheduled task %s", taskID)
					return
				}
			}
		}

	}()
	return taskID
}

func (s *Scheduler) WaitOnShutdown() {
	s.executor.WaitOnShutdown()
}
