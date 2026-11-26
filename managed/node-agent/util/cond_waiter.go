// Copyright (c) YugabyteDB, Inc.

package util

import (
	"context"
	"sync"
	"time"
)

// CondWaiter is a condition variable that can be used to wait for a condition to be met.
// Callers must hold Lock across Wait/TimedWait/Notify/NotifyAll, matching sync.Cond.
type CondWaiter struct {
	ctx   context.Context
	cond  *sync.Cond
	mutex *sync.Mutex
}

// TimedWaiter waits on a CondWaiter under a single overall deadline that spans
// multiple Wait calls (spurious or intermediate wakes do not reset the timeout).
type TimedWaiter struct {
	c      *CondWaiter
	ctx    context.Context
	cancel context.CancelFunc
}

// NewCondWaiter creates a new CondWaiter.
func NewCondWaiter(ctx context.Context) *CondWaiter {
	mutex := &sync.Mutex{}
	return &CondWaiter{
		ctx:   ctx,
		mutex: mutex,
		cond:  sync.NewCond(mutex),
	}
}

func (c *CondWaiter) Lock() {
	c.mutex.Lock()
}

func (c *CondWaiter) Unlock() {
	c.mutex.Unlock()
}

// Wait waits for the condition to be met indefinitely.
// Caller must hold Lock.
func (c *CondWaiter) Wait() {
	c.cond.Wait()
}

// TimedWait waits for the condition to be met or the timeout to be reached.
// Returns true if woken before the timeout, false if the timeout (or CondWaiter
// parent context cancellation) fired first. Caller must hold Lock.
func (c *CondWaiter) TimedWait(timeout time.Duration) bool {
	tw := c.CreateTimeWaiter(c.ctx, timeout)
	defer tw.Stop()
	return tw.Wait()
}

// CreateTimeWaiter creates a TimedWaiter whose deadline starts now and
// is shared across Wait calls. parent is the context that bounds the wait
// (timeout and cancellation); it is typically the RPC/task ctx rather than
// the CondWaiter's long-lived context. Caller must hold Lock when calling Wait.
// Call Stop when finished to release the deadline timer early.
func (c *CondWaiter) CreateTimeWaiter(parent context.Context, timeout time.Duration) *TimedWaiter {
	if parent == nil {
		parent = c.ctx
	}
	ctx, cancel := context.WithTimeout(parent, timeout)
	return &TimedWaiter{
		c:      c,
		ctx:    ctx,
		cancel: cancel,
	}
}

// Notify notifies one waiter.
// Caller must hold Lock.
func (c *CondWaiter) Notify() {
	c.cond.Signal()
}

// NotifyAll notifies all waiters.
// Caller must hold Lock.
func (c *CondWaiter) NotifyAll() {
	c.cond.Broadcast()
}

// Wait waits until notified or the overall deadline fires.
// Returns true if woken before the deadline, false if the deadline (or parent
// context cancellation) fired first. Caller must hold Lock.
func (t *TimedWaiter) Wait() bool {
	stopFunc := context.AfterFunc(t.ctx, func() {
		t.c.mutex.Lock()
		defer t.c.mutex.Unlock()
		t.c.cond.Broadcast()
	})
	t.c.cond.Wait()
	return stopFunc()
}

// Stop cancels the overall deadline timer.
func (t *TimedWaiter) Stop() {
	t.cancel()
}
