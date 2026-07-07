// Copyright (c) YugabyteDB, Inc.

package ybdb

import (
	"context"
	"io"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

// System abstracts the operating system interactions needed for detection so
// that the detector can be unit tested with fixtures.
type System interface {
	// Pgrep returns the pids whose full command line matches the pattern. No
	// match is not an error and returns an empty slice.
	Pgrep(ctx context.Context, pattern string) ([]int, error)
	// ReadCmdline returns the NUL separated arguments of a process.
	ReadCmdline(pid int) ([]string, error)
	// SsListeners returns the raw output of the listening socket sweep.
	SsListeners(ctx context.Context) (string, error)
	// StatFile reports whether the path exists and is readable.
	StatFile(path string) (exists bool, readable bool)
	// ReadFileLimit reads up to max bytes from the path.
	ReadFileLimit(path string, max int64) ([]byte, error)
	// SelfPid returns the pid of the current process.
	SelfPid() int
}

// osSystem is the production System backed by the local operating system.
type osSystem struct{}

func newOsSystem() System {
	return &osSystem{}
}

func (s *osSystem) Pgrep(ctx context.Context, pattern string) ([]int, error) {
	cmd := exec.CommandContext(ctx, "pgrep", "-f", pattern)
	out, err := cmd.Output()
	if err != nil {
		// pgrep exits with 1 when there is no match, which is not an error here.
		if exitErr, ok := err.(*exec.ExitError); ok && exitErr.ExitCode() == 1 {
			return nil, nil
		}
		return nil, err
	}
	pids := make([]int, 0)
	for _, line := range strings.Split(string(out), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		if pid, err := strconv.Atoi(line); err == nil {
			pids = append(pids, pid)
		}
	}
	return pids, nil
}

func (s *osSystem) ReadCmdline(pid int) ([]string, error) {
	data, err := os.ReadFile("/proc/" + strconv.Itoa(pid) + "/cmdline")
	if err != nil {
		return nil, err
	}
	parts := strings.Split(string(data), "\x00")
	args := make([]string, 0, len(parts))
	for _, part := range parts {
		if part != "" {
			args = append(args, part)
		}
	}
	return args, nil
}

func (s *osSystem) SsListeners(ctx context.Context) (string, error) {
	cmd := exec.CommandContext(ctx, "ss", "-H", "-O", "-lntp")
	out, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return string(out), nil
}

func (s *osSystem) StatFile(path string) (bool, bool) {
	info, err := os.Stat(path)
	if err != nil || info.IsDir() {
		return false, false
	}
	file, err := os.Open(path)
	if err != nil {
		return true, false
	}
	file.Close()
	return true, true
}

func (s *osSystem) ReadFileLimit(path string, max int64) ([]byte, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	return io.ReadAll(io.LimitReader(file, max))
}

func (s *osSystem) SelfPid() int {
	return os.Getpid()
}
