package runner

import (
	"strings"
	"testing"
)

func TestIdCounter(t *testing.T) {
	counter := newIdCounter()

	id1 := counter.getID(helper)
	if !strings.HasPrefix(id1, "1") {
		t.Error("helper id not indexed correctly")
	}
	id2 := counter.getID(helper)
	if !strings.HasPrefix(id2, "2") {
		t.Error("helper id not indexed correctly")
	}

	id3 := counter.getID(other)
	if !strings.HasPrefix(id3, "1") {
		t.Error("other id not indexed correctly")
	}
}

func helper() error {
	return nil
}

func other() error {
	return nil
}
