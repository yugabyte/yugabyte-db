/*
 * Copyright (c) YugabyteDB, Inc.
 */

package secretstore

import (
	"io"
	"os"
	"strings"
	"testing"
)

func TestReadPassword(t *testing.T) {
	t.Run("read from non-terminal", func(t *testing.T) {
		// Create a pipe to simulate non-terminal input
		r, w, err := os.Pipe()
		if err != nil {
			t.Fatalf("Pipe() failed: %v", err)
		}
		defer r.Close()
		defer w.Close()

		// Save original stdin
		oldStdin := os.Stdin
		defer func() { os.Stdin = oldStdin }()

		// Replace stdin with pipe
		os.Stdin = r

		// Write test input
		testInput := "testpassword\n"
		go func() {
			defer w.Close()
			io.WriteString(w, testInput)
		}()

		// Read password
		password, err := readPassword()
		if err != nil {
			t.Fatalf("readPassword() failed: %v", err)
		}

		expected := strings.TrimSpace(testInput)
		if password != expected {
			t.Errorf("readPassword() = %q, want %q", password, expected)
		}
	})
}

func TestPromptPassword(t *testing.T) {
	// Note: These tests are limited because promptPassword requires interactive input.
	// In a real scenario, these would be integration tests or use a more sophisticated
	// mocking approach. For now, we test the readPassword function which is the core
	// functionality that can be tested without interaction.
	t.Skip("Skipping interactive prompt tests - requires proper pipe handling")
}

func TestSetWithPrompt(t *testing.T) {
	// Note: SetWithPrompt requires interactive input which is difficult to test
	// with pipes in unit tests. The core functionality (validation, storage) is
	// tested in other test files. This would be better suited for integration tests.
	t.Skip("Skipping SetWithPrompt tests - requires proper interactive input handling")
}

