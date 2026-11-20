/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"maps"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestUpdateOCIEARAPIKeyFlags(t *testing.T) {
	keyPath := filepath.Join(t.TempDir(), "key.pem")
	if err := os.WriteFile(keyPath, []byte("PEM-CONTENT\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	tests := []struct {
		name    string
		args    []string
		want    map[string]interface{}
		wantErr string
	}{
		{
			name: "rotate signing key only",
			args: []string{"--fingerprint", "aa:bb", "--private-key-file-path", keyPath},
			want: map[string]interface{}{
				util.OCIFingerprintField:       "aa:bb",
				util.OCIPrivateKeyContentField: "PEM-CONTENT",
			},
		},
		{
			name: "all credentials",
			args: []string{
				"--tenancy-id", "ocid1.tenancy.oc1..example",
				"--user-id", "ocid1.user.oc1..example",
				"--fingerprint", "aa:bb",
				"--private-key-file-path", keyPath,
			},
			want: map[string]interface{}{
				util.OCITenancyIDField:         "ocid1.tenancy.oc1..example",
				util.OCIUserIDField:            "ocid1.user.oc1..example",
				util.OCIFingerprintField:       "aa:bb",
				util.OCIPrivateKeyContentField: "PEM-CONTENT",
			},
		},
		{
			name: "user only",
			args: []string{"--user-id", "ocid1.user.oc1..example"},
			want: map[string]interface{}{util.OCIUserIDField: "ocid1.user.oc1..example"},
		},
		{
			name: "no API key flags",
			args: []string{"--compartment-id", "ocid1.compartment.oc1..example"},
			want: map[string]interface{}{},
		},
		{
			name:    "fingerprint without private key",
			args:    []string{"--fingerprint", "aa:bb"},
			wantErr: "private-key-file-path",
		},
		{
			name: "empty value",
			args: []string{
				"--fingerprint", "", "--private-key-file-path", keyPath,
			},
			wantErr: "--fingerprint cannot be empty",
		},
		{
			name: "missing key file",
			args: []string{
				"--fingerprint", "aa:bb",
				"--private-key-file-path", filepath.Join(t.TempDir(), "missing.pem"),
			},
			wantErr: "missing.pem",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cmd := &cobra.Command{}
			addUpdateOCIEARFlags(cmd)
			if err := cmd.ParseFlags(tc.args); err != nil {
				t.Fatal(err)
			}
			err := cmd.ValidateFlagGroups()
			var got map[string]interface{}
			if err == nil {
				got, err = apiKeyUpdateFields(cmd)
			}
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("error = %v, want containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			if !maps.Equal(got, tc.want) {
				t.Fatalf("fields = %v, want %v", got, tc.want)
			}
		})
	}
}
