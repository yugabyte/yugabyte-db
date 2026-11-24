/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"testing"

	"github.com/spf13/cobra"
)

func TestGetInt32FlagIfChanged(t *testing.T) {
	cases := []struct {
		args []string
		want *int32
	}{
		{args: nil, want: nil},
		{args: []string{"--delay", "0"}, want: GetInt32Pointer(1)},
		{args: []string{"--delay", "500"}, want: GetInt32Pointer(500)},
	}
	*cases[1].want = 0
	for _, c := range cases {
		cmd := &cobra.Command{}
		cmd.Flags().Int32("delay", 18000, "")
		if err := cmd.Flags().Parse(c.args); err != nil {
			t.Fatalf("parse %v: %v", c.args, err)
		}
		got, err := GetInt32FlagIfChanged(cmd, "delay")
		if err != nil {
			t.Fatalf("args %v: %v", c.args, err)
		}
		switch {
		case c.want == nil && got != nil:
			t.Errorf("args %v: got %d, want nil", c.args, *got)
		case c.want != nil && (got == nil || *got != *c.want):
			t.Errorf("args %v: got %v, want %d", c.args, got, *c.want)
		}
	}
}
