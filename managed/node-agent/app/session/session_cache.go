// Copyright (c) YugaByte, Inc.

package session

import (
	"container/heap"
	"context"
	"node-agent/util"
	"sync"
	"time"
)

const (
	// DefaultCacheCapacity is the default capacity.
	DefaultCacheCapacity = 5
	// DeltaExpirySecs is the delta time for expiry consideration.
	DeltaExpirySecs = 10
)

var (
	instance *SessionCache
	once     = &sync.Once{}
)

// Init creates the singleton session cache.
func Init(ctx context.Context) *SessionCache {
	once.Do(func() {
		instance = NewSessionCache(ctx, DefaultCacheCapacity)
	})
	return instance
}

// GetCacheInstance returns the singleton session cache.
func GetCacheInstance() *SessionCache {
	if instance == nil {
		util.FileLogger().Fatal(nil, "Session cache is not initialized")
	}
	return instance
}

// SessionEntry is the cache entry for a session.
type SessionEntry struct {
	ID     string
	Info   any
	Expiry time.Time
	index  int // Internal.
}

// Index implements util.Indexable.
func (entry *SessionEntry) Index() int {
	return entry.index
}

// SetIndex implements util.Indexable.
func (entry *SessionEntry) SetIndex(index int) {
	entry.index = index
}

// SessionCache is the cache for sessions. Each entry has an expiry timestamp.
// Inserting an entry when the size reaches the capacity, evicts the one with
// the least expiry timestamp.
type SessionCache struct {
	ctx        context.Context
	mutex      *sync.Mutex
	timer      *time.Timer
	waitChan   chan struct{}
	capacity   int
	entryQueue *util.PriorityQueue[*SessionEntry]
	entryMap   map[string]*SessionEntry
}

// NewSessionCache returns an instance of SessionCache.
func NewSessionCache(ctx context.Context, capacity int) *SessionCache {
	cache := &SessionCache{
		ctx:      ctx,
		mutex:    &sync.Mutex{},
		capacity: capacity,
		waitChan: make(chan struct{}),
		entryQueue: util.NewPriorityQueue[*SessionEntry](
			func(e1, e2 *SessionEntry) bool { return e1.Expiry.Before(e2.Expiry) },
		),
		entryMap: map[string]*SessionEntry{},
	}
	heap.Init(cache.entryQueue)
	cache.startWatcher()
	return cache
}

// updateWatcher refreshes the watched entry.
func (cache *SessionCache) updateWatcher() {
	// Send without blocking if it is already full.
	select {
	case cache.waitChan <- struct{}{}:
	default:
	}
}

// startWatcher starts the watcher for the highest priority(top) entry.
func (cache *SessionCache) startWatcher() {
	createTimerFn := func() *time.Timer {
		cache.mutex.Lock()
		defer cache.mutex.Unlock()
		// Keep popping until there are no entries or a timer can be set up.
		for cache.entryQueue.Len() > 0 {
			top := cache.entryQueue.Peek()
			intervalSec := top.Expiry.Unix() - time.Now().Unix()
			if intervalSec <= 0 {
				item := heap.Pop(cache.entryQueue)
				entry := item.(*SessionEntry)
				delete(cache.entryMap, entry.ID)
				util.FileLogger().Infof(cache.ctx, "Expired session entry with key %s", entry.ID)
			} else {
				return time.NewTimer(time.Duration(time.Second * time.Duration(intervalSec)))
			}
		}
		return nil
	}
	expireFn := func() {
		cache.mutex.Lock()
		defer cache.mutex.Unlock()
		if cache.entryQueue.Len() > 0 {
			top := cache.entryQueue.Peek()
			// Additional safety check before deleting the entry.
			if top.Expiry.Unix()-time.Now().Unix() <= DeltaExpirySecs {
				item := heap.Pop(cache.entryQueue)
				entry := item.(*SessionEntry)
				delete(cache.entryMap, entry.ID)
				util.FileLogger().Infof(cache.ctx, "Expired session entry with key %s", entry.ID)
			}
		}
	}
	go func() {
		for {
			timer := createTimerFn()
			if cache.timer == nil {
				select {
				case <-cache.waitChan:
					if timer != nil {
						timer.Stop()
					}
				}
			} else {
				select {
				case <-cache.waitChan:
					if timer != nil {
						timer.Stop()
					}
				case <-cache.timer.C:
					expireFn()
				}
			}
		}
	}()
}

// Size returns the number of session entries in the cache.
func (cache *SessionCache) Size() int {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	return cache.entryQueue.Len()
}

// Get returns the session entry for the key.
func (cache *SessionCache) Get(ctx context.Context, key string) *SessionEntry {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	var entry *SessionEntry
	if val, ok := cache.entryMap[key]; ok {
		// Validate again if the entry is still valid.
		if val.Expiry.Before(time.Now()) {
			top := cache.entryQueue.Peek()
			delete(cache.entryMap, key)
			heap.Remove(cache.entryQueue, val.index)
			if top.ID == val.ID {
				// Update the watcher as the top is removed.
				cache.updateWatcher()
			}
		} else {
			entry = val
		}
	}
	return entry
}

// Put inserts the session entry into the cache.
func (cache *SessionCache) Put(ctx context.Context, entry *SessionEntry) {
	if entry == nil || entry.ID == "" || entry.Expiry.Before(time.Now()) {
		util.FileLogger().Error(ctx, "Invalid session cache entry")
		return
	}
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	if cache.entryQueue.Len() == 0 {
		heap.Push(cache.entryQueue, entry)
		cache.entryMap[entry.ID] = entry
		// Update the watcher as the top is added.
		cache.updateWatcher()
	} else if old, ok := cache.entryMap[entry.ID]; ok {
		top := cache.entryQueue.Peek()
		entry.index = old.index
		*old = *entry
		cache.entryMap[entry.ID] = entry
		heap.Fix(cache.entryQueue, entry.index)
		if top.ID == entry.ID {
			// Update the watcher as the top is updated.
			cache.updateWatcher()
		}
	} else {
		top := cache.entryQueue.Peek()
		if cache.entryQueue.Len() >= cache.capacity {
			entry.index = top.index
			*top = *entry
			cache.entryMap[entry.ID] = entry
			heap.Fix(cache.entryQueue, entry.index)
			// Update the watcher as the top is updated.
			cache.updateWatcher()
		} else {
			heap.Push(cache.entryQueue, entry)
			cache.entryMap[entry.ID] = entry
			if entry.Expiry.Before(top.Expiry) {
				// Update the watcher as the top is updated.
				cache.updateWatcher()
			}
		}
	}
}

// Pop returns the top after removing it.
func (cache *SessionCache) Pop() *SessionEntry {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	if cache.entryQueue.Len() > 0 {
		item := heap.Pop(cache.entryQueue)
		entry := item.(*SessionEntry)
		delete(cache.entryMap, entry.ID)
		cache.updateWatcher()
		return entry
	}
	return nil
}

// Peek returns the top without removing.
func (cache *SessionCache) Peek() *SessionEntry {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	return cache.entryQueue.Peek()
}

// Used in testing to drain into a slice.
func (cache *SessionCache) drain(ctx context.Context) []*SessionEntry {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	entries := make([]*SessionEntry, 0, cache.entryQueue.Len())
	for cache.entryQueue.Len() > 0 {
		item := heap.Pop(cache.entryQueue)
		entry := item.(*SessionEntry)
		delete(cache.entryMap, entry.ID)
		entries = append(entries, entry)
	}
	cache.updateWatcher()
	return entries
}
