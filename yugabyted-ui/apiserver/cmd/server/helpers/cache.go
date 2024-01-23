package helpers

import "sync"

// object for handling caching of single variables (int, string, etc.)
type VariableCache[T any] struct {
    value  T
    rwlock sync.RWMutex
}

func (c *VariableCache[T]) Get() T {
    c.rwlock.RLock()
    defer c.rwlock.RUnlock()
    return c.value
}

func (c *VariableCache[T]) Update(newValue T) {
    c.rwlock.Lock()
    defer c.rwlock.Unlock()
    c.value = newValue
}

// Cached variables
var (
    // Address of a tserver
    TserverAddressCache VariableCache[string]
)

// InitCache initializes cache variables above, called by NewHelperContainer
func (h *HelperContainer) InitCache() {
        TserverAddressCache.Update(HOST)
}
