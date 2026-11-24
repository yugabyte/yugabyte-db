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
		name        string
		sasToken    string
		updateCreds bool
		iam         bool
		env         *string
		want        string
		wantErr     string
	}{
		{name: "rename only keeps stored token", want: masked},
		{name: "rename with an env token set keeps stored token", env: ptr(envToken), want: masked},
		{name: "flag replaces stored token", sasToken: newToken, want: newToken},
		{
			name:        "flag wins over update-credentials",
			sasToken:    newToken,
			updateCreds: true,
			env:         ptr(envToken),
			want:        newToken,
		},
		{
			name:        "update-credentials reads env",
			updateCreds: true,
			env:         ptr(envToken),
			want:        envToken,
		},
		{name: "update-credentials without env", updateCreds: true, wantErr: "not found"},
		{
			name:        "update-credentials with empty env",
			updateCreds: true,
			env:         new(string),
			wantErr:     "is empty",
		},
		{
			name:        "update-credentials with IAM is ignored",
			updateCreds: true,
			iam:         true,
			env:         ptr(envToken),
			want:        masked,
		},
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
			err := applyAzureSASToken(data, tc.sasToken, tc.updateCreds)
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
