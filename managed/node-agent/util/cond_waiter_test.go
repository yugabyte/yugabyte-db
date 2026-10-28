// Copyright (c) YugabyteDB, Inc.

package util

import (
	"context"
	"sync"
	"testing"
	"time"
)

func TestCondWaiterTimedWaitTimeout(t *testing.T) {
	w := NewCondWaiter(context.Background())
	w.Lock()
	defer w.Unlock()

	start := time.Now()
	if w.TimedWait(100 * time.Millisecond) {
		t.Fatal("expected TimedWait to return false on timeout")
	}
	if elapsed := time.Since(start); elapsed < 80*time.Millisecond {
		t.Fatalf("TimedWait returned too early: %v", elapsed)
	}
}

func TestCondWaiterTimedWaitNotifyBeforeTimeout(t *testing.T) {
	w := NewCondWaiter(context.Background())

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		time.Sleep(50 * time.Millisecond)
		w.Lock()
		defer w.Unlock()
		w.NotifyAll()
	}()

	w.Lock()
	defer w.Unlock()
	if !w.TimedWait(2 * time.Second) {
		t.Fatal("expected TimedWait to return true after NotifyAll")
	}
	wg.Wait()
}

func TestCondWaiterNotifyWakesOneWaiter(t *testing.T) {
	w := NewCondWaiter(context.Background())
	woken := make(chan struct{}, 2)

	for i := 0; i < 2; i++ {
		go func() {
			w.Lock()
			defer w.Unlock()
			if w.TimedWait(2 * time.Second) {
				woken <- struct{}{}
			}
		}()
	}

	// Give waiters time to enter TimedWait.
	time.Sleep(50 * time.Millisecond)
	w.Lock()
	w.Notify()
	w.Unlock()

	select {
	case <-woken:
	case <-time.After(2 * time.Second):
		t.Fatal("expected one waiter to wake on Notify")
	}

	select {
	case <-woken:
		t.Fatal("Notify should wake only one waiter")
	case <-time.After(150 * time.Millisecond):
	}

	// Wake the remaining waiter so the goroutine can exit.
	w.Lock()
	w.NotifyAll()
	w.Unlock()
	select {
	case <-woken:
	case <-time.After(2 * time.Second):
		t.Fatal("expected remaining waiter to wake on NotifyAll")
	}
}

func TestCondWaiterNotifyAllWakesAllWaiters(t *testing.T) {
	w := NewCondWaiter(context.Background())
	const waiters = 3
	var wg sync.WaitGroup
	wg.Add(waiters)
	for i := 0; i < waiters; i++ {
		go func() {
			defer wg.Done()
			w.Lock()
			defer w.Unlock()
			if !w.TimedWait(2 * time.Second) {
				t.Error("expected TimedWait to return true after NotifyAll")
			}
		}()
	}

	time.Sleep(50 * time.Millisecond)
	w.Lock()
	w.NotifyAll()
	w.Unlock()

	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for all waiters")
	}
}

func TestCondWaiterParentContextCancel(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	w := NewCondWaiter(ctx)

	go func() {
		time.Sleep(50 * time.Millisecond)
		cancel()
	}()

	w.Lock()
	defer w.Unlock()
	if w.TimedWait(2 * time.Second) {
		t.Fatal("expected TimedWait to return false when parent context is cancelled")
	}
}

func TestTimedWaiterOverallDeadlineAcrossWakes(t *testing.T) {
	w := NewCondWaiter(context.Background())
	stopNotify := make(chan struct{})
	go func() {
		for {
			select {
			case <-stopNotify:
				return
			case <-time.After(40 * time.Millisecond):
				w.Lock()
				w.NotifyAll()
				w.Unlock()
			}
		}
	}()
	defer close(stopNotify)

	w.Lock()
	defer w.Unlock()
	tw := w.CreateTimeWaiter(context.Background(), 200*time.Millisecond)
	defer tw.Stop()

	start := time.Now()
	for tw.Wait() {
		// Intermediate wakes must not reset the overall deadline.
	}
	elapsed := time.Since(start)
	if elapsed < 150*time.Millisecond {
		t.Fatalf("timed out too early: %v", elapsed)
	}
	if elapsed > 400*time.Millisecond {
		t.Fatalf("overall deadline not honored, elapsed %v", elapsed)
	}
}

func TestTimedWaiterNotifyBeforeDeadline(t *testing.T) {
	w := NewCondWaiter(context.Background())
	go func() {
		time.Sleep(50 * time.Millisecond)
		w.Lock()
		defer w.Unlock()
		w.NotifyAll()
	}()

	w.Lock()
	defer w.Unlock()
	tw := w.CreateTimeWaiter(context.Background(), 2*time.Second)
	defer tw.Stop()
	if !tw.Wait() {
		t.Fatal("expected Wait to return true after NotifyAll")
	}
}

func TestCreateTimeWaiterParentContextCancel(t *testing.T) {
	// CondWaiter itself is long-lived; the TimedWaiter parent is what Abort cancels.
	w := NewCondWaiter(context.Background())
	parent, cancel := context.WithCancel(context.Background())

	go func() {
		time.Sleep(50 * time.Millisecond)
		cancel()
	}()

	w.Lock()
	defer w.Unlock()
	tw := w.CreateTimeWaiter(parent, 2*time.Second)
	defer tw.Stop()
	if tw.Wait() {
		t.Fatal("expected Wait to return false when TimedWaiter parent is cancelled")
	}
}
