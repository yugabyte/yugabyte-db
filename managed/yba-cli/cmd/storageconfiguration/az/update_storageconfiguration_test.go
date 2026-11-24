/*
 * Copyright (c) YugabyteDB, Inc.
 */

package azure

import (
	"os"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestApplyAzureSASToken(t *testing.T) {
	const (
		masked   = "sv**********************"
		newToken = "sv=2024-01-01&sig=newEXAMPLE"
		envToken = "sv=2024-01-01&sig=envEXAMPLE"
	)
	tests := []struct {
		name     string
		sasToken string
		iam      bool
		env      *string
		want     string
	}{
		{name: "rename only keeps stored token", want: masked},
		{name: "empty env keeps stored token", env: new(string), want: masked},
		{name: "env token replaces stored token", env: ptr(envToken), want: envToken},
		{name: "flag replaces stored token", sasToken: newToken, want: newToken},
		{name: "flag wins over env", sasToken: newToken, env: ptr(envToken), want: newToken},
		{name: "empty flag falls back to env", sasToken: "", env: ptr(envToken), want: envToken},
		{name: "IAM config ignores env token", iam: true, env: ptr(envToken), want: masked},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			// t.Setenv restores the variable after the test; Unsetenv makes it absent.
			t.Setenv(util.AzureStorageSasTokenEnv, "")
			if tc.env != nil {
				t.Setenv(util.AzureStorageSasTokenEnv, *tc.env)
			} else if err := os.Unsetenv(util.AzureStorageSasTokenEnv); err != nil {
				t.Fatal(err)
			}
			data := map[string]interface{}{util.AzureStorageSasTokenEnv: masked}
			if tc.iam {
				data[util.UseAzureIAM] = "true"
			}
			if err := applyAzureSASToken(data, tc.sasToken); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got := data[util.AzureStorageSasTokenEnv]; got != tc.want {
				t.Fatalf("got %v, want %v", got, tc.want)
			}
		})
	}
}

func ptr(s string) *string { return &s }
