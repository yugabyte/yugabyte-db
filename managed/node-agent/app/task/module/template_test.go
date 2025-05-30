// Copyright (c) YugaByte, Inc.

package module

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestServerTemplate(t *testing.T) {
	gflags := map[string]string{
		"key1": "value1",
		"key2": "value2",
		"key3": "value3",
	}
	gflagsContext := map[string]any{
		"gflags": gflags,
	}
	projectDir := os.Getenv("PROJECT_DIR")
	templatePath := filepath.Join(projectDir, "resources/templates/server/yb-server-gflags.j2")
	output, err := ResolveTemplate(
		context.TODO(),
		gflagsContext,
		templatePath,
	)
	if err != nil {
		t.Fatalf("Failed to copy file: %v", err)
	}
	t.Logf("Output: %s", output)
}
