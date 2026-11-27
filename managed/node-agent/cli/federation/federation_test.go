// Copyright (c) YugabyteDB, Inc.

package federation

import (
	"bytes"
	"errors"
	"fmt"
	"node-agent/util"
	"strings"
	"sync"
	"testing"

	"github.com/spf13/cobra"
)

var (
	testRoot     = &cobra.Command{Use: "node-agent", SilenceErrors: true, SilenceUsage: true}
	testRootOnce sync.Once
)

// rootCommand returns a root command carrying the federation commands. They are package state and
// registering their flags twice panics, so every test shares one registration.
func rootCommand() *cobra.Command {
	testRootOnce.Do(func() { SetupFederationCommand(testRoot) })
	testRoot.PersistentPreRunE = nil
	return testRoot
}

// The federation task shows only a failed step's stderr, so the reason must be on stderr.
func TestOciTokenFailureIsReportedOnStderr(t *testing.T) {
	root := rootCommand()
	var stderr bytes.Buffer
	root.SetErr(&stderr)
	root.SetArgs([]string{"federation", "oci-token", "--identity-domain-url",
		"http://x.example.com", "--output", "/x", "--fips_enabled=false"})
	if err := root.Execute(); err == nil {
		t.Fatal("an insecure identity domain must fail")
	}
	if !strings.Contains(stderr.String(), "identity domain URL must be") {
		t.Errorf("the reason is missing from stderr: %q", stderr.String())
	}
}

// The root command's FIPS hook takes the mode from --fips_enabled, which the setup step and the
// refresher unit always pass, so it never reads a node-agent config the DB user may not have.
func TestOciTokenFipsModeComesFromItsFlag(t *testing.T) {
	for _, want := range []bool{true, false} {
		root := rootCommand()
		var got *bool
		root.PersistentPreRunE = func(cmd *cobra.Command, _ []string) error {
			enabled := util.NodeAgentFipsEnabled(cmd.Flags())
			got = &enabled
			return errors.New("stop before the exchange")
		}
		root.SetErr(&bytes.Buffer{})
		root.SetArgs([]string{"federation", "oci-token", "--identity-domain-url",
			"https://x.example.com", "--output", "/x", fmt.Sprintf("--fips_enabled=%t", want)})
		_ = root.Execute()
		if got == nil || *got != want {
			t.Errorf("--fips_enabled=%t: the FIPS hook read %v", want, got)
		}
	}
}
