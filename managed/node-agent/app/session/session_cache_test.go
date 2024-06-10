// Copyright (c) YugaByte, Inc.

package session

import (
	"context"
	"math/rand"
	"strconv"
	"testing"
	"time"
)

func TestSessionCacheOrder(t *testing.T) {
	ctx := context.TODO()
	cache := NewSessionCache(ctx, 30)
	for i := 0; i < 50; i++ {
		// Insert multiple times for the same key.
		for j := 0; j < 3; j++ {
			entry := &SessionEntry{
				ID:     strconv.Itoa(i),
				Expiry: time.Now().Add(time.Duration(time.Minute * time.Duration(1+rand.Intn(10)))),
			}
			cache.Put(ctx, entry)
		}
	}
	if cache.Size() != 30 {
		t.Fatalf("Cache size (%d) is not exactly 30", cache.Size())
	}
	for i := 0; i < 30; i++ {
		key := strconv.Itoa(i)
		val := cache.Get(ctx, key)
		if val == nil {
			t.Fatalf("Value is not found for %s", key)
		}
	}
	entries := make([]*SessionEntry, 0, cache.Size())
	set := map[string]struct{}{}
	var prevEntry *SessionEntry
	for cache.Size() > 0 {
		top := cache.Peek()
		entry := cache.Pop()
		if top.ID != entry.ID {
			t.Fatalf("Top and popped entries are not the same")
		}
		entries = append(entries, entry)
		if prevEntry == nil {
			prevEntry = entry
		} else {
			if _, ok := set[entry.ID]; ok {
				t.Fatalf("Duplicate entry found for %s", entry.ID)
			}
			if prevEntry.Expiry.After(entry.Expiry) {
				t.Fatalf("Entries are not sorted at %s", entry.ID)
			}
		}
		set[entry.ID] = struct{}{}
	}
	for i := 1; i < len(entries); i++ {
		t.Logf("%+v", *entries[i])
	}
}

func TestSessionCacheExpiry(t *testing.T) {
	ctx := context.TODO()
	cache := NewSessionCache(ctx, 30)
	entryCount := 3
	expiryStep := time.Second * 4
	now := time.Now()
	expiryTimes := make([]time.Time, 0, entryCount)
	for i := 0; i < 5; i++ {
		expiryTimes = append(expiryTimes, now.Add(expiryStep))
	}
	for i, expiryTime := range expiryTimes {
		entry := &SessionEntry{
			ID:     strconv.Itoa(i),
			Expiry: expiryTime,
		}
		cache.Put(ctx, entry)
	}
	expectedSizeAfterExpiry := entryCount - 1
	for i, expiryTime := range expiryTimes {
		sleepTime := expiryTime.Add(time.Second).Sub(time.Now())
		time.Sleep(sleepTime)
		entry := cache.Get(ctx, strconv.Itoa(i))
		if entry != nil && cache.Size() != expectedSizeAfterExpiry {
			entries := cache.drain(ctx)
			for _, entry := range entries {
				t.Logf("After sleep index %d - %+v", i, *entry)
			}
			t.Fatalf("Not expired or wrong session expired")
		}
		expectedSizeAfterExpiry--
	}
}
