package helpers

import "sync"

// object for handling caching slices
type SliceCache[T any] struct {
    slice  []T
    rwlock sync.RWMutex
}

func (c *SliceCache[T]) Get() []T {
    c.rwlock.RLock()
    defer c.rwlock.RUnlock()
    // make a copy to prevent r/w access to original slice
    slice := make([]T, len(c.slice))
    copy(slice, c.slice)
    return slice
}

func (c *SliceCache[T]) Update(newSlice []T) {
    c.rwlock.Lock()
    defer c.rwlock.Unlock()
    // create a new slice and copy in the contents
    c.slice = make([]T, len(newSlice))
    copy(c.slice, newSlice)
}

// Cached variables
var (
    // Addresses of all tservers
    TserverAddressCache SliceCache[string]

    // Addresses of all masters
    MasterAddressCache SliceCache[string]
)

// InitCache initializes cache variables above, called by NewHelperContainer
func (h *HelperContainer) InitCache() {
    TserverAddressCache.Update([]string{HOST})
    MasterAddressCache.Update([]string{HOST})
}
