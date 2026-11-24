/*
 * Copyright (c) YugabyteDB, Inc.
 */

package azure

import (
	"os"
	"strings"
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
		flagSet  bool
		env      *string
		want     string
		wantErr  string
	}{
		{name: "rename only keeps stored token", want: masked},
		{name: "empty env keeps stored token", env: new(string), want: masked},
		{name: "env token replaces stored token", env: ptr(envToken), want: envToken},
		{name: "flag replaces stored token", sasToken: newToken, flagSet: true, want: newToken},
		{
			name:     "flag wins over env",
			sasToken: newToken,
			flagSet:  true,
			env:      ptr(envToken),
			want:     newToken,
		},
		{name: "empty flag", flagSet: true, wantErr: "must be non-empty"},
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
			err := applyAzureSASToken(data, tc.sasToken, tc.flagSet)
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("got error %v, want one containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got := data[util.AzureStorageSasTokenEnv]; got != tc.want {
				t.Fatalf("got %v, want %v", got, tc.want)
			}
		})
	}
}

func ptr(s string) *string { return &s }
