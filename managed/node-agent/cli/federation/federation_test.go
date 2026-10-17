// Copyright (c) YugabyteDB, Inc.

package federation

import (
	"bytes"
	"strings"
	"testing"

	"github.com/spf13/cobra"
)

// The federation task shows only a failed step's stderr, so the reason must be on stderr.
func TestOciTokenFailureIsReportedOnStderr(t *testing.T) {
	root := &cobra.Command{Use: "node-agent", SilenceErrors: true}
	SetupFederationCommand(root)
	var stderr bytes.Buffer
	root.SetErr(&stderr)
	root.SetArgs([]string{
		"federation", "oci-token", "--identity-domain-url", "http://x.example.com", "--output", "/x"})
	if err := root.Execute(); err == nil {
		t.Fatal("an insecure identity domain must fail")
	}
	if !strings.Contains(stderr.String(), "identity domain URL must be") {
		t.Errorf("the reason is missing from stderr: %q", stderr.String())
	}
}
