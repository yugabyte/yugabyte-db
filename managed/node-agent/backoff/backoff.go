// Copyright (c) YugabyteDB, Inc.

package backoff

import (
	"context"
	"time"
)

// BackOff interface for managing retry delays and limits.
type BackOff interface {
	// NextBackOff returns the next backoff duration.
	NextBackOff(attempt int) time.Duration
	// Reset resets the backoff state.
	Reset()
}

// Do executes a function with retries and backoff.
func Do(ctx context.Context, backOff BackOff, fn func(int) error) error {
	for attempt := 1; ; attempt++ {
		if err := fn(attempt); err != nil {
			nextBackOff := backOff.NextBackOff(attempt)
			if nextBackOff < 0 {
				return err
			}
			select {
			case <-time.After(nextBackOff):
				continue
			case <-ctx.Done():
				return err
			}
		}
		return nil
	}
}

// SimpleBackOff is a simple constant backoff implementation of the BackOff interface.
type SimpleBackOff struct {
	interval   time.Duration
	maxAttempt int
}

// NewSimpleBackOff creates a new SimpleBackOff.
func NewSimpleBackOff(interval time.Duration, maxAttempt int) *SimpleBackOff {
	return &SimpleBackOff{interval: interval, maxAttempt: maxAttempt}
}

// NextBackOff implements BackOff.
func (b *SimpleBackOff) NextBackOff(attempt int) time.Duration {
	if attempt >= b.maxAttempt {
		return -1
	}
	return b.interval
}

// Reset implements BackOff.
func (b *SimpleBackOff) Reset() {
}
