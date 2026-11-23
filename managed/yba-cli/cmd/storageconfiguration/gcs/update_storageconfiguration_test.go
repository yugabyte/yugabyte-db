/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcs

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func writeCredsFile(t *testing.T, clientEmail string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "creds.json")
	body := `{"type": "service_account", "project_id": "p", "client_email": "` +
		clientEmail + `", "private_key": "k"}`
	if err := os.WriteFile(path, []byte(body), 0600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestApplyGCSCreds(t *testing.T) {
	const masked = "{ \"cl****************************\" }"
	fileCreds := writeCredsFile(t, "file@example.com")
	envCreds := writeCredsFile(t, "env@example.com")
	tests := []struct {
		name        string
		filePath    string
		fileFlagSet bool
		updateCreds bool
		wasIAM      bool
		isIAM       bool
		envFile     string
		// wantEmail is the client_email the stored JSON must hold; "" means unchanged.
		wantEmail string
		wantGone  bool
		wantErr   string
	}{
		{name: "rename only keeps stored credentials"},
		{
			name:        "file replaces stored credentials",
			filePath:    fileCreds,
			fileFlagSet: true,
			wantEmail:   "file@example.com",
		},
		{
			name:        "empty file flag",
			fileFlagSet: true,
			wantErr:     "must be non-empty",
		},
		{
			name:        "update-credentials reads env",
			updateCreds: true,
			envFile:     envCreds,
			wantEmail:   "env@example.com",
		},
		{
			name:        "update-credentials without env",
			updateCreds: true,
			wantErr:     util.GCPCredentialsEnv,
		},
		{
			name:        "file wins over update-credentials",
			filePath:    fileCreds,
			fileFlagSet: true,
			updateCreds: true,
			envFile:     envCreds,
			wantEmail:   "file@example.com",
		},
		{
			name:      "switching off IAM reads env",
			wasIAM:    true,
			envFile:   envCreds,
			wantEmail: "env@example.com",
		},
		{
			name:     "switching to IAM drops stored credentials",
			isIAM:    true,
			wantGone: true,
		},
		{
			name:        "update-credentials with IAM is ignored",
			updateCreds: true,
			isIAM:       true,
			wantGone:    true,
		},
		{
			name:        "file flag with IAM",
			filePath:    fileCreds,
			fileFlagSet: true,
			isIAM:       true,
			wantErr:     "cannot be used with GCP IAM",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Setenv(util.GCPCredentialsEnv, tc.envFile)
			data := map[string]interface{}{util.GCSCredentialsJSON: masked}
			err := applyGCSCreds(
				data, tc.filePath, tc.fileFlagSet, tc.updateCreds, tc.wasIAM, tc.isIAM)
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("got error %v, want one containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			got, ok := data[util.GCSCredentialsJSON]
			switch {
			case tc.wantGone:
				if ok {
					t.Fatalf("credentials kept: %v", got)
				}
			case tc.wantEmail == "":
				if got != masked {
					t.Fatalf("got %v, want stored credentials kept", got)
				}
			default:
				if s, _ := got.(string); !strings.Contains(s, tc.wantEmail) {
					t.Fatalf("got %v, want credentials for %s", got, tc.wantEmail)
				}
			}
		})
	}
}
