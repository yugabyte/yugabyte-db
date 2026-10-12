// Copyright (c) YugabyteDB, Inc.

package scheduler

import (
	"context"
	"node-agent/app/executor"
	"testing"
	"time"
)

func TestScheduler(t *testing.T) {
	ctx, cancelFunc := context.WithCancel(context.Background())
	defer cancelFunc()
	executor.Init(ctx)
	Init(ctx)
	instance := GetInstance()

	t.Run("afterInterval", func(t *testing.T) {
		ch := make(chan int, 1)
		start := time.Now()
		instance.Schedule(
			ctx,
			time.Second*2,
			false, /* runImmediately */
			func(ctx context.Context) (any, error) {
				ch <- 1
				return nil, nil
			},
		)
		count := 0
		maxCount := 3
		for range ch {
			count++
			if count >= maxCount {
				break
			}
		}
		elapasedTime := time.Since(start)
		expectedMinTime := time.Duration(int(time.Second)*2*maxCount - 1)
		if elapasedTime < expectedMinTime {
			t.Fatalf(
				"Elapsed time (%d) expected to be greater than %d",
				elapasedTime,
				expectedMinTime,
			)
		}
	})

	t.Run("runImmediately", func(t *testing.T) {
		ch := make(chan struct{}, 1)
		instance.Schedule(
			ctx,
			time.Second*5,
			true, /* runImmediately */
			func(ctx context.Context) (any, error) {
				ch <- struct{}{}
				return nil, nil
			},
		)
		select {
		case <-ch:
			// First run should not wait for the 5s interval.
		case <-time.After(2 * time.Second):
			t.Fatal("Expected immediate first run within 2s")
		}
	})

	cancelFunc()
	instance.WaitOnShutdown()
}
