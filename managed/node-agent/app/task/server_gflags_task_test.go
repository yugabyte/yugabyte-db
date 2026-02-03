// Copyright (c) YugabyteDB, Inc.

package task

import (
	"path/filepath"
	"testing"
)

func TestBuildPostmasterCgroupPath(t *testing.T) {
	tests := []struct {
		name              string
		cgroupStatOutput  string
		userID            string
		expectedPath      string
	}{
		{
			name:             "cgroup v1 returns memory path",
			cgroupStatOutput: "tmpfs",
			userID:           "1000",
			expectedPath:     "/sys/fs/cgroup/memory/ysql",
		},
		{
			name:             "cgroup v1 empty output returns memory path",
			cgroupStatOutput: "",
			userID:           "1000",
			expectedPath:     "/sys/fs/cgroup/memory/ysql",
		},
		{
			name:             "cgroup v2 returns unified path with user ID",
			cgroupStatOutput: "cgroup2fs",
			userID:           "1000",
			expectedPath:     filepath.Join("/sys/fs/cgroup", "user.slice", "user-1000.slice", "user@1000.service", "ysql"),
		},
		{
			name:             "cgroup v2 with different user ID",
			cgroupStatOutput: "cgroup2fs",
			userID:           "1001",
			expectedPath:     filepath.Join("/sys/fs/cgroup", "user.slice", "user-1001.slice", "user@1001.service", "ysql"),
		},
		{
			name:             "cgroup v2 with root user",
			cgroupStatOutput: "cgroup2fs",
			userID:           "0",
			expectedPath:     filepath.Join("/sys/fs/cgroup", "user.slice", "user-0.slice", "user@0.service", "ysql"),
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := buildPostmasterCgroupPath(tt.cgroupStatOutput, tt.userID)
			if got != tt.expectedPath {
				t.Errorf("buildPostmasterCgroupPath(%q, %q) = %q, want %q",
					tt.cgroupStatOutput, tt.userID, got, tt.expectedPath)
			}
		})
	}
}
