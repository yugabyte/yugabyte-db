// Copyright (c) YugabyteDB, Inc.

package util

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
)

const serverLockFile = "node-agent.lock"

// ErrLockHeld is returned when another process holds the lock.
var ErrLockHeld = errors.New("lock is held by another process")

// ServerLockPath returns the lock file that allows only one node agent server per install.
func ServerLockPath() string {
	return filepath.Join(ConfigDir(), serverLockFile)
}

// TryLockFile takes a non-blocking exclusive flock on path and writes this pid into it.
// The kernel drops the lock when the process exits, so a crash never leaves it stale.
// The caller must keep the returned file referenced: closing it (or letting it be garbage
// collected) releases the lock.
func TryLockFile(path string) (*os.File, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return nil, err
	}
	// No O_TRUNC: the file must not be emptied before the lock is ours, or the holder's pid
	// would be lost for the error message.
	file, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, 0644)
	if err != nil {
		return nil, err
	}
	if err := syscall.Flock(int(file.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		holder := readLockHolder(file)
		file.Close()
		if errors.Is(err, syscall.EWOULDBLOCK) {
			return nil, fmt.Errorf("%w (pid %s)", ErrLockHeld, holder)
		}
		return nil, err
	}
	if err := file.Truncate(0); err == nil {
		file.WriteAt([]byte(strconv.Itoa(os.Getpid())+"\n"), 0)
	}
	return file, nil
}

func readLockHolder(file *os.File) string {
	buf := make([]byte, 32)
	n, _ := file.ReadAt(buf, 0)
	if pid := strings.TrimSpace(string(buf[:n])); pid != "" {
		return pid
	}
	return "unknown"
}
