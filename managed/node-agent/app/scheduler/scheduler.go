// Copyright (c) YugabyteDB, Inc.

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
)

type Scheduler struct {
	ctx      context.Context
	executor *executor.TaskExecutor
	tasks    *sync.Map
}

type taskInfo struct {
	mutex     *sync.Mutex
	id        uuid.UUID
	handler   util.Handler
	interval  time.Duration
	ticker    *time.Ticker
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

func Init(ctx context.Context) *Scheduler {
	once.Do(func() {
		instance = &Scheduler{ctx: ctx, tasks: &sync.Map{}, executor: executor.GetInstance()}
	})
	return instance
}

func GetInstance() *Scheduler {
	if instance == nil {
		util.FileLogger().Fatal(context.TODO(), "Scheduler is not initialized")
	}
	return instance
}

func (s *Scheduler) executeTask(ctx context.Context, taskID uuid.UUID) error {
	value, ok := s.tasks.Load(taskID)
	if !ok {
		err := fmt.Errorf("Invalid state for task %s. Exiting...", taskID)
		util.FileLogger().Error(ctx, err.Error())
		return err
	}
	info := value.(*taskInfo)
	if !info.compareAndSetRunning(true) {
		util.FileLogger().Warnf(ctx, "Task %s is still running", taskID)
		return nil
	}
	defer info.compareAndSetRunning(false)
	err := executor.GetInstance().ExecuteTask(ctx, info.handler)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Failed to submit job %s. Error: %s", taskID, err)
		return err
	}
	return nil
}

// Schedule runs handler on a fixed interval.
// If runImmediately is true, handler runs once before waiting for the first tick;
// otherwise the first run is after interval.
func (s *Scheduler) Schedule(
	ctx context.Context,
	interval time.Duration,
	runImmediately bool,
	handler util.Handler,
) uuid.UUID {
	scheduleID := util.NewUUID()
	taskInfo := &taskInfo{
		mutex:    &sync.Mutex{},
		id:       scheduleID,
		interval: interval,
		handler:  handler,
	}
	s.tasks.Store(scheduleID, taskInfo)
	go func() {
		taskInfo.ticker = time.NewTicker(interval)
		defer s.tasks.Delete(scheduleID)
		defer taskInfo.ticker.Stop()
		if runImmediately {
			if err := s.executeTask(ctx, scheduleID); err != nil {
				util.FileLogger().
					Errorf(ctx, "Failed immediate run for scheduled task %s", scheduleID)
			}
		}
		for {
			select {
			case <-ctx.Done():
				return
			case <-s.ctx.Done():
				return
			case <-taskInfo.ticker.C:
				err := s.executeTask(ctx, scheduleID)
				if err != nil {
					util.FileLogger().Errorf(ctx, "Exiting scheduled task %s", scheduleID)
				}
			}
		}
	}()
	return scheduleID
}

func (s *Scheduler) IsScheduleActive(scheduleID uuid.UUID) bool {
	_, ok := s.tasks.Load(scheduleID)
	return ok
}

func (s *Scheduler) WaitOnShutdown() {
	s.executor.WaitOnShutdown()
}
