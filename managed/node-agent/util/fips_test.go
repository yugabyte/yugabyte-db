// Copyright (c) YugabyteDB, Inc.

package util

import (
	"reflect"
	"testing"

	"github.com/spf13/pflag"
)

func TestFipsEnviron(t *testing.T) {
	got := fipsEnviron([]string{"PATH=/bin", "GODEBUG=http2client=0,fips140=on"}, false)
	want := []string{fipsReexecEnv + "=1", "PATH=/bin", "GODEBUG=http2client=0,fips140=off"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("fipsEnviron() = %v, want %v", got, want)
	}
	got = fipsEnviron([]string{"PATH=/bin"}, true)
	want = []string{fipsReexecEnv + "=1", "PATH=/bin", "GODEBUG=fips140=on"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("fipsEnviron() = %v, want %v", got, want)
	}
}

func TestNodeAgentFipsEnabled(t *testing.T) {
	config := CurrentConfig()
	defer config.Remove(PlatformFipsEnabledKey)
	newFlags := func(args ...string) *pflag.FlagSet {
		flags := pflag.NewFlagSet("test", pflag.ContinueOnError)
		flags.Bool("fips_enabled", false, "")
		if err := flags.Parse(args); err != nil {
			t.Fatal(err)
		}
		return flags
	}
	if NodeAgentFipsEnabled(newFlags()) {
		t.Fatal("no stored value must mean not FIPS")
	}
	if !NodeAgentFipsEnabled(newFlags("--fips_enabled")) {
		t.Fatal("--fips_enabled must mean FIPS")
	}
	config.Update(PlatformFipsEnabledKey, true)
	if !NodeAgentFipsEnabled(pflag.NewFlagSet("no-flag", pflag.ContinueOnError)) {
		t.Fatal("the stored value must apply when the command has no --fips_enabled")
	}
	if NodeAgentFipsEnabled(newFlags("--fips_enabled=false")) {
		t.Fatal("--fips_enabled=false must override the stored value")
	}
}
