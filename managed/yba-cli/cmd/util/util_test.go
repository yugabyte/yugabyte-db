/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import "testing"

func TestIsTrueValue(t *testing.T) {
	tests := []struct {
		name  string
		value interface{}
		want  bool
	}{
		{name: "missing key", value: nil, want: false},
		{name: "bool true", value: true, want: true},
		{name: "bool false", value: false, want: false},
		{name: "string true", value: "true", want: true},
		{name: "string false", value: "false", want: false},
		{name: "unparsable string", value: "yes", want: false},
		{name: "other type", value: 1, want: false},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if got := IsTrueValue(tc.value); got != tc.want {
				t.Fatalf("IsTrueValue(%v) = %v, want %v", tc.value, got, tc.want)
			}
		})
	}
}
