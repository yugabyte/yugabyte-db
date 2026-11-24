/*
 * Copyright (c) YugabyteDB, Inc.
 */

package storageconfiguration

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// The IAM flags can be stored as a JSON bool, a string, or not at all; none may panic.
func TestIAMGetters(t *testing.T) {
	tests := []struct {
		name  string
		value interface{}
		set   bool
		want  string
	}{
		{name: "bool", value: true, set: true, want: "true"},
		{name: "string", value: "false", set: true, want: "false"},
		{name: "null", value: nil, set: true, want: "-"},
		{name: "missing", want: "-"},
	}
	getters := map[string]func(*Context) string{
		util.IAMInstanceProfile: (*Context).IAMInstanceProfile,
		util.UseGCPIAM:          (*Context).UseGCPIAM,
	}
	for key, get := range getters {
		for _, tc := range tests {
			t.Run(key+"/"+tc.name, func(t *testing.T) {
				data := map[string]interface{}{}
				if tc.set {
					data[key] = tc.value
				}
				c := &Context{s: ybaclient.CustomerConfigUI{Data: data}}
				if got := get(c); got != tc.want {
					t.Fatalf("got %q, want %q", got, tc.want)
				}
			})
		}
	}
}
