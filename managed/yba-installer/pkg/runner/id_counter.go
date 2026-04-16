package runner

import (
	"fmt"
	"reflect"
	"runtime"
)

type idCounter struct {
	m map[string]int
}

func newIdCounter() *idCounter {
	return &idCounter{make(map[string]int)}
}

// getID will create an id for the given argument, and track it in the counter
func (c *idCounter) getID(a any) string {
	baseID := runtime.FuncForPC(reflect.ValueOf(a).Pointer()).Name()
	return fmt.Sprintf("%d-%s", c.addInc(baseID), baseID)
}

func (c *idCounter) addInc(val string) int {
	if _, ok := c.m[val]; ok {
		c.m[val]++
	} else {
		c.m[val] = 1
	}
	return c.m[val]
}
