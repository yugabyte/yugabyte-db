/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"testing"

	"github.com/spf13/cobra"
)

func TestGetInt32FlagIfChanged(t *testing.T) {
	zero, value := int32(0), int32(500)
	cases := []struct {
		args []string
		want *int32
	}{
		{args: nil, want: nil},
		{args: []string{"--delay", "0"}, want: &zero},
		{args: []string{"--delay", "500"}, want: &value},
	}
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
