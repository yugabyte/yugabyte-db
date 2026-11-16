// Copyright (c) YugabyteDB, Inc.

package common

import (
	"reflect"
	"testing"
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
