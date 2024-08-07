// Copyright (c) YugaByte, Inc.

package scheduler

import (
	"context"
	"node-agent/app/executor"
	"testing"
	"time"
)

func TestScheduler(t *testing.T) {
	ctx, cancelFunc := context.WithCancel(context.Background())
	executor.Init(ctx)
	Init(ctx)
	instance := GetInstance()
	ch := make(chan int, 1)
	start := time.Now()
	instance.Schedule(ctx, time.Second*2, func(ctx context.Context) (any, error) {
		ch <- 1
		return nil, nil
	})
	count := 0
	maxCount := 3
loop:
	for {
		select {
		case <-ch:
			count++
			if count >= maxCount {
				break loop
			}
		}
	}
	elapasedTime := time.Since(start)
	expectedMinTime := time.Duration(int(time.Second)*2*maxCount - 1)
	if elapasedTime < expectedMinTime {
		t.Fatalf("Elapsed time (%d) expected to be lesser than %d", elapasedTime, expectedMinTime)
	}
	cancelFunc()
	instance.WaitOnShutdown()
}
