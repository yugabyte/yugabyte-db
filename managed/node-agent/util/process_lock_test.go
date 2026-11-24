// Copyright (c) YugabyteDB, Inc.

package util

import (
	"errors"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func TestTryLockFileAllowsOneHolder(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sub", "server.lock")
	first, err := TryLockFile(path)
	if err != nil {
		t.Fatalf("first lock failed: %v", err)
	}
	// flock locks belong to the open file description, so a second open in the same process
	// conflicts just like a second process would.
	_, err = TryLockFile(path)
	if !errors.Is(err, ErrLockHeld) {
		t.Fatalf("expected ErrLockHeld, got %v", err)
	}
	if !strings.Contains(err.Error(), strconv.Itoa(os.Getpid())) {
		t.Fatalf("error %q does not name the holder pid", err)
	}
	first.Close()
	second, err := TryLockFile(path)
	if err != nil {
		t.Fatalf("lock after release failed: %v", err)
	}
	second.Close()
}
